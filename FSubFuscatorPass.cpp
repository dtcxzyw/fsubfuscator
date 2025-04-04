/*
    SPDX-License-Identifier: Apache-2.0

    Copyright 2023 Yingwei Zheng
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
        http://www.apache.org/licenses/LICENSE-2.0
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "BitRep.hpp"
#include "FSubFuscatorPass.hpp"
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringSwitch.h>
#include <llvm/Analysis/DomTreeUpdater.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/EarlyCSE.h>
#include <llvm/Transforms/Scalar/InstSimplifyPass.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Local.h>
#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

cl::OptionCategory FsubFuscatorCategory("fsub fuscator options");

static cl::opt<BitRepMethod> RepMethod(
    "bitrep", cl::desc("Bit representation to use"),
    cl::values(clEnumVal(FSub, "Default: Use fsub and f32. (T=0.0, F=-0.0)"),
               clEnumVal(Int1, "Use bitwise and i1. (T=true, F=false)"),
               clEnumVal(InvInt1, "Use bitwise and i1. (T=false, F=true)"),
               clEnumVal(Mod3, "Use mod and i32. (T=2, F=1)")),
    cl::init(DefaultBitRep), cl::cat(FsubFuscatorCategory));

class BitFuscatorImpl final : public InstVisitor<BitFuscatorImpl, Value *> {
  Function &F;
  IRBuilder<> Builder;
  std::unique_ptr<BitRepBase> BitRep;
  std::unordered_map<Value *, std::vector<Value *>> CachedCastToBit,
      CachedCastFromBit;
  DominatorTree &DT;
  DomTreeUpdater DTU;

  Value *convertToBit(Value *V) {
    if (auto It = CachedCastToBit.find(V); It != CachedCastToBit.end()) {
      auto *Block = Builder.GetInsertBlock();
      for (auto *Res : It->second) {
        if (!isa<Instruction>(Res))
          return Res;
        auto *I = cast<Instruction>(Res);
        if (I->getParent() == Block || DT.dominates(I, Block))
          return Res;
      }
    }

    assert(!V->getType()->isVectorTy());
    auto *VT = VectorType::get(Builder.getInt1Ty(),
                               V->getType()->getScalarSizeInBits(),
                               /*Scalable=*/false);
    auto *Bits = Builder.CreateBitCast(V, VT);
    auto *Res = BitRep->convertToBit(Bits);
    CachedCastToBit[V].push_back(Res);
    CachedCastFromBit[Res].push_back(V);
    return Res;
  }
  Value *convertFromBit(Value *V, Type *DestTy) {
    if (auto It = CachedCastFromBit.find(V); It != CachedCastFromBit.end()) {
      auto *Block = Builder.GetInsertBlock();
      for (auto *Res : It->second) {
        if (Res->getType() != DestTy)
          continue;

        if (!isa<Instruction>(Res))
          return Res;
        auto *I = cast<Instruction>(Res);
        if (I->getParent() == Block || DT.dominates(I, Block))
          return Res;
      }
    }

    assert(V->getType()->isVectorTy() && !DestTy->isVectorTy());
    auto *Bits = BitRep->convertFromBit(V);
    auto *Res = Builder.CreateBitCast(Bits, DestTy);
    if (F.getParent()->getDataLayout().isBigEndian())
      Res = Builder.CreateUnaryIntrinsic(Intrinsic::bswap, Res);
    CachedCastToBit[Res].push_back(V);
    CachedCastFromBit[V].push_back(Res);
    return Res;
  }
  std::pair<Value *, Value *> fullAdder(Value *A, Value *B, Value *Carry) {
    auto *Xor = BitRep->bitXor(A, B);
    auto *Sum = BitRep->bitXor(Xor, Carry);
    auto *CarryOut =
        BitRep->bitOr(BitRep->bitAnd(Xor, Carry), BitRep->bitAnd(A, B));
    return {Sum, CarryOut};
  }
  std::pair<Value *, Value *> addWithOverflowBits(Value *Op1, Value *Op2,
                                                  bool Sub, bool Unsigned,
                                                  unsigned Bits) {
    if (Sub)
      Op2 = BitRep->bitNot(Op2);
    Value *Carry = Sub ? BitRep->getBit1() : BitRep->getBit0();
    Value *LastCarry = nullptr;

    Value *Res = PoisonValue::get(Op1->getType());
    for (int I = 0; I < Bits; ++I) {
      auto *A = Builder.CreateExtractElement(Op1, I);
      auto *B = Builder.CreateExtractElement(Op2, I);
      auto [Sum, CarryOut] = fullAdder(A, B, Carry);
      Res = Builder.CreateInsertElement(Res, Sum, I);
      LastCarry = Carry;
      Carry = CarryOut;
    }
    if (Unsigned) {
      if (Sub)
        Carry = BitRep->bitNot(Carry);
    } else
      Carry = BitRep->bitXor(Carry, LastCarry);
    return {Res, Carry};
  }
  std::pair<Value *, Value *> addWithOverflow(Value *V1, Value *V2, bool Sub,
                                              bool Unsigned) {
    auto *Op1 = convertToBit(V1);
    auto *Op2 = convertToBit(V2);
    auto [Res, Carry] = addWithOverflowBits(
        Op1, Op2, Sub, Unsigned, V1->getType()->getScalarSizeInBits());
    auto *ResVal = convertFromBit(Res, V1->getType());
    auto *CarryVal = BitRep->convertFromBit(Carry);
    return {ResVal, CarryVal};
  }
  Value *nonZero(Value *Bits) {
    return Builder.CreateOrReduce(BitRep->convertFromBit(Bits));
  }
  Value *equalZero(Value *Bits) { return Builder.CreateNot(nonZero(Bits)); }
  Value *lessThanZero(Value *Bits) {
    return BitRep->convertFromBit(Builder.CreateExtractElement(
        Bits,
        cast<VectorType>(Bits->getType())->getElementCount().getFixedValue() -
            1));
  }
  Value *lshr1(Value *Bits) {
    VectorType *VT = dyn_cast<VectorType>(Bits->getType());
    SmallVector<int, 64> Mask(VT->getElementCount().getFixedValue());
    std::iota(Mask.begin(), Mask.end(), 1);
    return Builder.CreateShuffleVector(
        Bits, getConstantWithType(VT, BitRep->getBit0()), Mask);
  }
  Value *ashr1(Value *Bits) {
    VectorType *VT = dyn_cast<VectorType>(Bits->getType());
    unsigned BitWidth = VT->getElementCount().getFixedValue();
    SmallVector<int, 64> Mask(BitWidth);
    std::iota(Mask.begin(), Mask.begin() + BitWidth - 1, 1);
    Mask.back() = BitWidth - 1;
    return Builder.CreateShuffleVector(Bits, Mask);
  }
  Value *shl1(Value *Bits) {
    VectorType *VT = dyn_cast<VectorType>(Bits->getType());
    unsigned BitWidth = VT->getElementCount().getFixedValue();
    SmallVector<int, 64> Mask(BitWidth);
    Mask.front() = BitWidth;
    std::iota(Mask.begin() + 1, Mask.end(), 0);
    return Builder.CreateShuffleVector(
        Bits, getConstantWithType(Bits->getType(), BitRep->getBit0()), Mask);
  }
  Value *mult(Value *V1, Value *V2) {
    auto *Op1 = convertToBit(V1);
    auto *Op2 =
        convertToBit(Builder.CreateFreeze(V2)); // Branch on undef/poison is UB.

    auto Bits = V1->getType()->getScalarSizeInBits();
    // sum = 0;
    // while(op2 != 0) { sum += op2 & 1 ? op1 : 0; op1 = op1 + op1; op2 >>= 1; }

    auto *Block = Builder.GetInsertBlock();
    auto InsertPt = std::next(Builder.GetInsertPoint());
    auto *Post = SplitBlock(Block, InsertPt, &DTU);
    auto *Header = BasicBlock::Create(F.getContext(), "mul.header", &F);
    auto *Body = BasicBlock::Create(F.getContext(), "mul.body", &F);
    auto *Zero = getConstantWithType(Op1->getType(), BitRep->getBit0());
    SmallVector<DominatorTree::UpdateType, 8> DTUpdates;

    // Pre
    Block->getTerminator()->setSuccessor(0, Header);
    DTUpdates.push_back({DominatorTree::Delete, Block, Post});
    DTUpdates.push_back({DominatorTree::Insert, Block, Header});
    // Header
    Builder.SetInsertPoint(Header);
    auto *Res = Builder.CreatePHI(Op1->getType(), 2);
    auto *A = Builder.CreatePHI(Op1->getType(), 2);
    auto *B = Builder.CreatePHI(Op1->getType(), 2);
    auto *Cond = nonZero(B);
    Builder.CreateCondBr(Cond, Body, Post);
    DTUpdates.push_back({DominatorTree::Insert, Header, Body});
    DTUpdates.push_back({DominatorTree::Insert, Header, Post});
    // Body
    Builder.SetInsertPoint(Body);
    auto *IsOdd = BitRep->convertFromBit(
        Builder.CreateExtractElement(B, Builder.getInt32(0)));
    auto *Add = Builder.CreateSelect(IsOdd, A, Zero);
    auto *Sum =
        addWithOverflowBits(Res, Add, /*Sub=*/false, /*Unsigned=*/true, Bits)
            .first;
    auto *NextA =
        addWithOverflowBits(A, A, /*Sub=*/false, /*Unsigned=*/true, Bits).first;
    auto *NextB = lshr1(B);
    Builder.CreateBr(Header);
    DTUpdates.push_back({DominatorTree::Insert, Body, Header});
    // Phi nodes
    Res->addIncoming(Zero, Block);
    A->addIncoming(Op1, Block);
    B->addIncoming(Op2, Block);

    Res->addIncoming(Sum, Body);
    A->addIncoming(NextA, Body);
    B->addIncoming(NextB, Body);

    // Post
    Builder.SetInsertPoint(Post, Post->getFirstInsertionPt());
    DTU.applyUpdates(DTUpdates);
    return convertFromBit(Res, V1->getType());
  }

  // C++ code for unsigned div/mod:
  // std::pair<unsigned, unsigned> udivmod(unsigned a, unsigned b) {
  //   if (b == 0)
  //     __builtin_trap();
  //   unsigned bit = 1;
  //   while (b < a && (((int)b) > 0)) {
  //     b <<= 1;
  //     bit <<= 1;
  //   }
  //   unsigned q = 0;
  //   while (true) {
  //     if (a >= b) {
  //       a -= b;
  //       q |= bit;
  //     }
  //     b >>= 1;
  //     bit >>= 1;
  //     if (bit == 0)
  //       break;
  //   }
  //   return {q, a};
  // }

  std::pair<Value *, Value *> udivmod(Value *V1, Value *V2) {
    auto *Op1 = convertToBit(V1);
    auto *Op2 = convertToBit(V2);

    auto Bits = V1->getType()->getScalarSizeInBits();

    auto *Block = Builder.GetInsertBlock();
    auto InsertPt = std::next(Builder.GetInsertPoint());
    auto *Post = SplitBlock(Block, InsertPt, &DTU);
    auto *ShiftHeader =
        BasicBlock::Create(F.getContext(), "divmod.shift_header", &F);
    auto *ShiftBody =
        BasicBlock::Create(F.getContext(), "divmod.shift_body", &F);
    auto *SubstractHeader =
        BasicBlock::Create(F.getContext(), "divmod.substract_header", &F);
    auto *SubstractBody =
        BasicBlock::Create(F.getContext(), "divmod.substract_body", &F);
    auto *DividedByZero =
        BasicBlock::Create(F.getContext(), "divmod.divided_by_zero", &F);
    auto *Zero = getConstantWithType(Op1->getType(), BitRep->getBit0());
    auto *One = Builder.CreateInsertElement(Zero, BitRep->getBit1(),
                                            Builder.getInt64(0));
    SmallVector<DominatorTree::UpdateType, 8> DTUpdates;

    // Pre
    Block->getTerminator()->eraseFromParent();
    DTUpdates.push_back({DominatorTree::Delete, Block, Post});
    Builder.SetInsertPoint(Block);
    auto *IsZero = equalZero(Op2);
    Builder.CreateCondBr(IsZero, DividedByZero, ShiftHeader);
    DTUpdates.push_back({DominatorTree::Insert, Block, DividedByZero});
    DTUpdates.push_back({DominatorTree::Insert, Block, ShiftHeader});
    // DividedByZero
    Builder.SetInsertPoint(DividedByZero);
    Builder.CreateIntrinsic(Intrinsic::trap,{});
    Builder.CreateUnreachable();
    // ShiftHeader
    Builder.SetInsertPoint(ShiftHeader);
    // b u< a && b s> 0
    auto EvalCondExpr = [&](Value *A, Value *B) {
      auto [Res, Carry] =
          addWithOverflowBits(B, A,
                              /*Sub=*/true, /*Unsigned=*/true, Bits);
      auto *Cond1 = BitRep->convertFromBit(Carry);
      auto *Cond2 = Builder.CreateNot(lessThanZero(B));
      auto *Cond = Builder.CreateAnd(Cond1, Cond2);
      return Cond;
    };
    Builder.CreateCondBr(EvalCondExpr(Op1, Op2), ShiftBody, SubstractHeader);
    DTUpdates.push_back({DominatorTree::Insert, ShiftHeader, SubstractHeader});
    DTUpdates.push_back({DominatorTree::Insert, ShiftHeader, ShiftBody});
    // ShiftBody
    Builder.SetInsertPoint(ShiftBody);
    auto *B = Builder.CreatePHI(Op1->getType(), 2);
    auto *Bit = Builder.CreatePHI(Op1->getType(), 2);
    auto *NextB = shl1(B);
    auto *NextBit = shl1(Bit);
    // NextB u< A && NextB s> 0
    Builder.CreateCondBr(EvalCondExpr(Op1, NextB), ShiftBody, SubstractHeader);
    DTUpdates.push_back({DominatorTree::Insert, ShiftBody, SubstractHeader});
    DTUpdates.push_back({DominatorTree::Insert, ShiftBody, ShiftBody});
    B->addIncoming(Op2, ShiftHeader);
    Bit->addIncoming(One, ShiftHeader);
    B->addIncoming(NextB, ShiftBody);
    Bit->addIncoming(NextBit, ShiftBody);
    // SubstractHeader
    Builder.SetInsertPoint(SubstractHeader);
    auto *InitB = Builder.CreatePHI(Op1->getType(), 2);
    auto *InitBit = Builder.CreatePHI(Op1->getType(), 2);
    InitB->addIncoming(Op2, ShiftHeader);
    InitBit->addIncoming(One, ShiftHeader);
    InitB->addIncoming(NextB, ShiftBody);
    InitBit->addIncoming(NextBit, ShiftBody);
    Builder.CreateBr(SubstractBody);
    DTUpdates.push_back(
        {DominatorTree::Insert, SubstractHeader, SubstractBody});
    // SubstractBody
    Builder.SetInsertPoint(SubstractBody);
    auto *PhiA = Builder.CreatePHI(Op1->getType(), 2);
    auto *PhiB = Builder.CreatePHI(Op1->getType(), 2);
    auto *PhiBit = Builder.CreatePHI(Op1->getType(), 2);
    auto *PhiRes = Builder.CreatePHI(Op1->getType(), 2);
    // a u< b
    auto [Sub, Carry] =
        addWithOverflowBits(PhiA, PhiB, /*Sub=*/true, /*Unsigned=*/true, Bits);
    auto *Cond = BitRep->convertFromBit(Carry);
    auto *NextPhiA = Builder.CreateSelect(Cond, PhiA, Sub);
    auto *NextPhiRes =
        Builder.CreateSelect(Cond, PhiRes, BitRep->bitOr(PhiRes, PhiBit));
    auto *NextPhiBit = lshr1(PhiBit);
    auto *NextPhiB = lshr1(PhiB);
    Builder.CreateCondBr(equalZero(NextPhiBit), Post, SubstractBody);
    DTUpdates.push_back({DominatorTree::Insert, SubstractBody, Post});
    DTUpdates.push_back({DominatorTree::Insert, SubstractBody, SubstractBody});
    PhiA->addIncoming(Op1, SubstractHeader);
    PhiB->addIncoming(InitB, SubstractHeader);
    PhiBit->addIncoming(InitBit, SubstractHeader);
    PhiRes->addIncoming(Zero, SubstractHeader);
    PhiA->addIncoming(NextPhiA, SubstractBody);
    PhiB->addIncoming(NextPhiB, SubstractBody);
    PhiBit->addIncoming(NextPhiBit, SubstractBody);
    PhiRes->addIncoming(NextPhiRes, SubstractBody);
    // Post
    Builder.SetInsertPoint(Post, Post->getFirstInsertionPt());
    DTU.applyUpdates(DTUpdates);

    auto *Quotient = convertFromBit(NextPhiRes, V1->getType());
    auto *Remainder = convertFromBit(NextPhiA, V1->getType());
    return {Quotient, Remainder};
  }

  // C++ code for signed div/mod:
  // std::pair<int, int> sdivmod(int a, int b) {
  //   bool NegB = false;
  //   if (b < 0) {
  //     b = -b;
  //     NegB = true;
  //   }
  //   bool NegA = false;
  //   if (a < 0) {
  //     a = -a;
  //     NegA = true;
  //   }
  //   auto [q, r] = udivmod(a, b);
  //   if (NegA)
  //     r = -r;
  //   if (NegA ^ NegB)
  //     q = -q;
  //   return {q, r};
  // }

  std::pair<Value *, Value *> sdivmod(Value *V1, Value *V2) {
    // TODO: use Bits
    auto *NegA =
        Builder.CreateICmpSLT(V1, Constant::getNullValue(V1->getType()));
    auto *NegB =
        Builder.CreateICmpSLT(V2, Constant::getNullValue(V2->getType()));
    auto *A = Builder.CreateSelect(NegA, Builder.CreateNeg(V1), V1);
    auto *B = Builder.CreateSelect(NegB, Builder.CreateNeg(V2), V2);
    auto [Quotient, Remainder] = udivmod(A, B);
    auto *NegRemainder = Builder.CreateNeg(Remainder);
    auto *NegQuotient = Builder.CreateNeg(Quotient);
    auto *FinalRemainder = Builder.CreateSelect(NegA, NegRemainder, Remainder);
    auto *FinalQuotient = Builder.CreateSelect(Builder.CreateXor(NegA, NegB),
                                               NegQuotient, Quotient);
    return {FinalQuotient, FinalRemainder};
  }

public:
  // rewrites
  Value *visitInstruction(Instruction &I) { return nullptr; }
  Value *visitAdd(BinaryOperator &I) {
    return addWithOverflow(I.getOperand(0), I.getOperand(1), /*Sub=*/false,
                           /*Unsigned=*/true)
        .first;
  }
  Value *visitSub(BinaryOperator &I) {
    return addWithOverflow(I.getOperand(0), I.getOperand(1), /*Sub=*/true,
                           /*Unsigned=*/true)
        .first;
  }
  Value *visitMul(BinaryOperator &I) {
    return mult(I.getOperand(0), I.getOperand(1));
  }
  Value *visitSDiv(BinaryOperator &I) {
    auto *V0 = Builder.CreateFreeze(I.getOperand(0));
    auto *V1 = Builder.CreateFreeze(I.getOperand(1));
    return sdivmod(V0, V1).first;
  }
  Value *visitUDiv(BinaryOperator &I) {
    auto *V0 = Builder.CreateFreeze(I.getOperand(0));
    auto *V1 = Builder.CreateFreeze(I.getOperand(1));
    return udivmod(V0, V1).first;
  }
  Value *visitSRem(BinaryOperator &I) {
    auto *V0 = Builder.CreateFreeze(I.getOperand(0));
    auto *V1 = Builder.CreateFreeze(I.getOperand(1));
    return sdivmod(V0, V1).second;
  }
  Value *visitURem(BinaryOperator &I) {
    auto *V0 = Builder.CreateFreeze(I.getOperand(0));
    auto *V1 = Builder.CreateFreeze(I.getOperand(1));
    return udivmod(V0, V1).second;
  }
  template <typename ShiftOnce>
  Value *visitShift(Type *DestTy, Value *Src, Value *ShAmtVal, ShiftOnce Func,
                    bool ExtractHigh = false) {
    auto *ShAmt = Builder.CreateFreeze(ShAmtVal);

    auto *Block = Builder.GetInsertBlock();
    auto InsertPt = std::next(Builder.GetInsertPoint());
    auto *Post = SplitBlock(Block, InsertPt, &DTU);
    auto *Header = BasicBlock::Create(F.getContext(), "shift.header", &F);
    auto *Body = BasicBlock::Create(F.getContext(), "shift.body", &F);
    SmallVector<DominatorTree::UpdateType, 8> DTUpdates;

    // Pre
    Block->getTerminator()->setSuccessor(0, Header);
    DTUpdates.push_back({DominatorTree::Delete, Block, Post});
    DTUpdates.push_back({DominatorTree::Insert, Block, Header});
    // Header
    Builder.SetInsertPoint(Header);
    auto *IndVar = Builder.CreatePHI(ShAmtVal->getType(), 2);
    auto *Res = Builder.CreatePHI(Src->getType(), 2);
    auto *Cond = Builder.CreateICmpNE(IndVar, ShAmt);
    Builder.CreateCondBr(Cond, Body, Post);
    DTUpdates.push_back({DominatorTree::Insert, Header, Body});
    DTUpdates.push_back({DominatorTree::Insert, Header, Post});
    // Body
    Builder.SetInsertPoint(Body);
    auto *NextIndVar =
        Builder.CreateAdd(IndVar, ConstantInt::get(IndVar->getType(), 1));
    auto *NextRes = Func(Res);
    Builder.CreateBr(Header);
    DTUpdates.push_back({DominatorTree::Insert, Body, Header});
    // Phi nodes
    IndVar->addIncoming(ConstantInt::getNullValue(IndVar->getType()), Block);
    Res->addIncoming(Src, Block);

    IndVar->addIncoming(NextIndVar, Body);
    Res->addIncoming(NextRes, Body);

    // Post
    Builder.SetInsertPoint(Post, Post->getFirstInsertionPt());
    Value *FinalRes = Res;
    const auto DestBits = DestTy->getScalarSizeInBits();
    // Trunc result for funnel shifts
    if (DestBits !=
        cast<VectorType>(Res->getType())->getElementCount().getFixedValue()) {
      SmallVector<int, 64> Mask(DestBits);
      std::iota(Mask.begin(), Mask.end(), ExtractHigh ? DestBits : 0);
      FinalRes = Builder.CreateShuffleVector(FinalRes, Mask);
    }
    DTU.applyUpdates(DTUpdates);
    return convertFromBit(FinalRes, DestTy);
  }
  Value *getReducedShAmt(Value *ShAmt) {
    return Builder.CreateBinaryIntrinsic(
        Intrinsic::umin, ShAmt,
        ConstantInt::get(ShAmt->getType(),
                         ShAmt->getType()->getScalarSizeInBits()));
  }
  Value *visitShl(BinaryOperator &I) {
    return visitShift(I.getType(), convertToBit(I.getOperand(0)),
                      getReducedShAmt(I.getOperand(1)),
                      [&](Value *V) { return shl1(V); });
  }
  Value *visitAShr(BinaryOperator &I) {
    return visitShift(
        I.getType(), convertToBit(Builder.CreateFreeze(I.getOperand(0))),
        getReducedShAmt(I.getOperand(1)), [&](Value *V) { return ashr1(V); });
  }
  Value *visitLShr(BinaryOperator &I) {
    return visitShift(I.getType(), convertToBit(I.getOperand(0)),
                      getReducedShAmt(I.getOperand(1)),
                      [&](Value *V) { return lshr1(V); });
  }
  Value *visitAnd(BinaryOperator &I) {
    if (I.getType()->isVectorTy())
      return nullptr;
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep->bitAnd(Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitOr(BinaryOperator &I) {
    if (I.getType()->isVectorTy())
      return nullptr;
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep->bitOr(Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitXor(BinaryOperator &I) {
    if (I.getType()->isVectorTy())
      return nullptr;
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep->bitXor(Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitCast(CastInst &I, bool NullOp1, bool Freeze, ArrayRef<int> Mask) {
    auto *V = I.getOperand(0);
    if (Freeze)
      V = Builder.CreateFreeze(V);
    auto *Op0 = convertToBit(V);
    auto *Res = Builder.CreateShuffleVector(
        Op0,
        NullOp1 ? getConstantWithType(Op0->getType(), BitRep->getBit0())
                : PoisonValue::get(Op0->getType()),
        Mask);
    return convertFromBit(Res, I.getType());
  }
  Value *visitTrunc(TruncInst &I) {
    auto DestBits = I.getType()->getScalarSizeInBits();
    SmallVector<int, 64> Mask(DestBits);
    std::iota(Mask.begin(), Mask.end(), 0);
    return visitCast(I, /*NullOp1=*/false, /*Freeze=*/false, Mask);
  }
  Value *visitZExt(ZExtInst &I) {
    auto DestBits = I.getType()->getScalarSizeInBits();
    auto SrcBits = I.getOperand(0)->getType()->getScalarSizeInBits();
    SmallVector<int, 64> Mask(DestBits);
    std::iota(Mask.begin(), Mask.begin() + SrcBits, 0);
    std::fill(Mask.begin() + SrcBits, Mask.end(), SrcBits);
    return visitCast(I, /*NullOp1=*/true, /*Freeze=*/false, Mask);
  }
  Value *visitSExt(SExtInst &I) {
    auto DestBits = I.getType()->getScalarSizeInBits();
    auto SrcBits = I.getOperand(0)->getType()->getScalarSizeInBits();
    SmallVector<int, 64> Mask(DestBits);
    std::iota(Mask.begin(), Mask.begin() + SrcBits, 0);
    std::fill(Mask.begin() + SrcBits, Mask.end(), SrcBits - 1);
    return visitCast(I, /*NullOp1=*/false, /*Freeze=*/true, Mask);
  }
  Value *visitICmp(ICmpInst &I) {
    if (!I.getOperand(0)->getType()->isIntegerTy())
      return nullptr;

    auto *Op0 = I.getOperand(0);
    auto *Op1 = I.getOperand(1);
    if (I.isRelational()) {
      Op0 = Builder.CreateFreeze(Op0);
      Op1 = Builder.CreateFreeze(Op1);
    }
    auto Pred = I.getPredicate();
    if (ICmpInst::getStrictPredicate(I.getUnsignedPredicate()) ==
        ICmpInst::ICMP_UGT) {
      std::swap(Op0, Op1);
      Pred = ICmpInst::getSwappedPredicate(Pred);
    }

    auto [Res, Carry] = addWithOverflow(
        Op0, Op1,
        /*Sub=*/true, /*Unsigned=*/I.isEquality() || I.isUnsigned());
    // TODO: use Bits?
    switch (Pred) {
    case ICmpInst::ICMP_EQ:
      return Builder.CreateICmpEQ(Res, Constant::getNullValue(Res->getType()));
    case ICmpInst::ICMP_NE:
      return Builder.CreateICmpNE(Res, Constant::getNullValue(Res->getType()));
    case ICmpInst::ICMP_ULT:
      return Carry;
    case ICmpInst::ICMP_ULE:
      return Builder.CreateOr(
          Carry,
          Builder.CreateICmpEQ(Res, Constant::getNullValue(Res->getType())));
    case ICmpInst::ICMP_SLT:
      return Builder.CreateXor(
          Carry,
          Builder.CreateICmpSLT(Res, Constant::getNullValue(Res->getType())));
    case ICmpInst::ICMP_SLE:
      return Builder.CreateXor(
          Carry,
          Builder.CreateICmpSLE(Res, Constant::getNullValue(Res->getType())));
    default:
      llvm_unreachable("Unexpected ICmp predicate");
    }
  }
  Value *visitSelect(SelectInst &I) {
    if (!I.getType()->isIntegerTy())
      return nullptr;

    auto *TrueV = convertToBit(I.getTrueValue());
    auto *FalseV = convertToBit(I.getFalseValue());
    auto *Select = Builder.CreateSelect(I.getCondition(), TrueV, FalseV);
    return convertFromBit(Select, I.getType());
  }
  Value *visitPhi(PHINode &Phi) {
    if (!Phi.getType()->isIntegerTy())
      return nullptr;
    auto *NewPHI =
        Builder.CreatePHI(VectorType::get(BitRep->getBitTy(),
                                          Phi.getType()->getScalarSizeInBits(),
                                          /*Scalable=*/false),
                          Phi.getNumIncomingValues());
    for (uint32_t I = 0; I != Phi.getNumIncomingValues(); ++I) {
      auto *Incoming = Phi.getIncomingValue(I);
      auto *IncomingBlock = Phi.getIncomingBlock(I);
      IRBuilder<>::InsertPointGuard Guard(Builder);
      Builder.SetInsertPoint(IncomingBlock->getTerminator());
      auto *IncomingValue = convertToBit(Incoming);
      NewPHI->addIncoming(IncomingValue, IncomingBlock);
    }
    return NewPHI;
  }
  Value *visitIntrinsicInst(IntrinsicInst &I) {
    Intrinsic::ID IID = I.getIntrinsicID();
    switch (IID) {
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::usub_with_overflow: {
      auto *LHS = Builder.CreateFreeze(I.getOperand(0));
      auto *RHS = Builder.CreateFreeze(I.getOperand(1));
      auto [Res, Overflow] =
          addWithOverflow(LHS, RHS,
                          /*Sub=*/IID == Intrinsic::usub_with_overflow,
                          /*Unsigned=*/true);
      auto *Pair =
          Builder.CreateInsertValue(PoisonValue::get(I.getType()), Res, {0});
      return Builder.CreateInsertValue(Pair, Overflow, {1});
    }
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::ssub_with_overflow: {
      auto *LHS = Builder.CreateFreeze(I.getOperand(0));
      auto *RHS = Builder.CreateFreeze(I.getOperand(1));
      auto [Res, Overflow] =
          addWithOverflow(LHS, RHS,
                          /*Sub=*/IID == Intrinsic::ssub_with_overflow,
                          /*Unsigned=*/false);
      auto *Pair =
          Builder.CreateInsertValue(PoisonValue::get(I.getType()), Res, {0});
      return Builder.CreateInsertValue(Pair, Overflow, {1});
    }
    case Intrinsic::ctpop: {
      auto *Bits = BitRep->convertFromBit(convertToBit(I.getOperand(0)));
      auto *ZExt = Builder.CreateZExt(
          Bits, Bits->getType()->getWithNewType(I.getType()));
      // FIXME: use BitRep
      return Builder.CreateAddReduce(ZExt);
    }
    case Intrinsic::fshl:
    case Intrinsic::fshr: {
      auto *Op0 = I.getOperand(0);
      auto *Op1 = I.getOperand(1);
      Op0 = Builder.CreateFreeze(Op0);
      Op1 = Builder.CreateFreeze(Op1);

      auto *BitsA = convertToBit(Op0);
      auto *BitsB = convertToBit(Op1);
      std::swap(BitsA, BitsB);
      auto BitWidth = I.getOperand(0)->getType()->getScalarSizeInBits();
      SmallVector<int, 128> Mask(BitWidth * 2);
      std::iota(Mask.begin(), Mask.end(), 0);
      auto *Combined = Builder.CreateShuffleVector(BitsA, BitsB, Mask);
      auto *Shamt = I.getOperand(2);
      return visitShift(
          I.getType(), Combined,
          Builder.CreateURem(
              Shamt, ConstantInt::get(Shamt->getType(),
                                      Shamt->getType()->getScalarSizeInBits())),
          [&](Value *V) {
            if (I.getIntrinsicID() == Intrinsic::fshl)
              return shl1(V);
            return lshr1(V);
          },
          /*ExtractHigh=*/I.getIntrinsicID() == Intrinsic::fshl);
    }
    case Intrinsic::abs: {
      auto *Op0 = I.getOperand(0);
      if (cast<ConstantInt>(I.getOperand(1))->isZero())
        Op0 = Builder.CreateFreeze(Op0);
      auto BitWidth = Op0->getType()->getScalarSizeInBits();
      auto *Bits = convertToBit(Op0);
      auto *Sign = Builder.CreateExtractElement(Bits, BitWidth - 1);
      auto *Mask = Builder.CreateSelect(
          BitRep->convertFromBit(Sign),
          getConstantWithType(Bits->getType(), BitRep->getBit1()),
          getConstantWithType(Bits->getType(), BitRep->getBit0()));
      // abs(x) = (x + sign) ^ sign
      auto *Sum = addWithOverflowBits(Bits, Mask, /*Sub=*/false,
                                      /*Unsigned=*/true, BitWidth)
                      .first;
      auto *Res = BitRep->bitXor(Sum, Mask);
      return convertFromBit(Res, I.getType());
    }
    case Intrinsic::bitreverse: {
      auto *Bits = convertToBit(I.getOperand(0));
      auto *Res = Builder.CreateVectorReverse(Bits);
      return convertFromBit(Res, I.getType());
    }
    case Intrinsic::smin:
    case Intrinsic::smax:
    case Intrinsic::umin:
    case Intrinsic::umax: {
      auto *Op0 = Builder.CreateFreeze(I.getOperand(0));
      auto *Op1 = Builder.CreateFreeze(I.getOperand(1));
      auto Signed = MinMaxIntrinsic::isSigned(IID);
      auto Bits = Op0->getType()->getScalarSizeInBits();
      auto *LHS = convertToBit(Op0);
      auto *RHS = convertToBit(Op1);
      auto [Res, Carry] =
          addWithOverflowBits(LHS, RHS, /*Sub=*/true, !Signed, Bits);
      Value *LessThan;
      if (Signed)
        LessThan =
            Builder.CreateXor(BitRep->convertFromBit(Carry), lessThanZero(Res));
      else
        LessThan = BitRep->convertFromBit(Carry);
      Value *ResVal = nullptr;
      if (IID == Intrinsic::smin || IID == Intrinsic::umin)
        ResVal = Builder.CreateSelect(LessThan, LHS, RHS);
      else
        ResVal = Builder.CreateSelect(LessThan, RHS, LHS);
      return convertFromBit(ResVal, I.getType());
    }
    case Intrinsic::sadd_sat:
    case Intrinsic::ssub_sat:
    case Intrinsic::uadd_sat:
    case Intrinsic::usub_sat: {
      bool IsSub = IID == Intrinsic::ssub_sat || IID == Intrinsic::usub_sat;
      bool IsUnsigned =
          IID == Intrinsic::uadd_sat || IID == Intrinsic::usub_sat;
      auto *Op0 = Builder.CreateFreeze(I.getOperand(0));
      auto *Op1 = Builder.CreateFreeze(I.getOperand(1));
      auto [Res, Carry] =
          addWithOverflow(Op0, Op1, /*Sub=*/IsSub, /*Unsigned=*/IsUnsigned);
      auto Bits = Op0->getType()->getScalarSizeInBits();
      APInt SatVal = IsSub ? (IsUnsigned ? APInt::getMinValue(Bits)
                                         : APInt::getSignedMinValue(Bits))
                           : (IsUnsigned ? APInt::getMaxValue(Bits)
                                         : APInt::getSignedMaxValue(Bits));
      return Builder.CreateSelect(Carry, Builder.getInt(SatVal), Res);
    }
    case Intrinsic::ucmp:
    case Intrinsic::scmp: {
      if (!I.getOperand(0)->getType()->isIntegerTy())
        return nullptr;
      auto *Op0 = Builder.CreateFreeze(I.getOperand(0));
      auto *Op1 = Builder.CreateFreeze(I.getOperand(1));
      auto [Res, Carry] = addWithOverflow(Op0, Op1, /*Sub=*/true,
                                          /*Unsigned=*/IID == Intrinsic::ucmp);
      // cmp = zext(x > y) - zext(x < y)
      Value *GT = nullptr;
      Value *LT = nullptr;
      if (IID == Intrinsic::ucmp) {
        GT = Builder.CreateAnd(Builder.CreateIsNotNull(Res),
                               Builder.CreateNot(Carry));
        LT = Carry;
      } else {
        GT = Builder.CreateXor(
            Carry,
            Builder.CreateICmpSGT(Res, Constant::getNullValue(Res->getType())));
        LT = Builder.CreateXor(
            Carry,
            Builder.CreateICmpSLT(Res, Constant::getNullValue(Res->getType())));
      }
      return Builder.CreateSub(Builder.CreateZExt(GT, I.getType()),
                               Builder.CreateZExt(LT, I.getType()));
    }
    default:
      return nullptr;
    }
  }

  static BitRepMethod getRepMethod() {
    // Resolve from environment variable
    auto *Env = getenv("FSUBFUSCATOR_BITREP_OVERRIDE");
    if (Env) {
      return StringSwitch<BitRepMethod>(StringRef{Env})
          .Case("Int1", BitRepMethod::Int1)
          .Case("InvInt1", BitRepMethod::InvInt1)
          .Case("FSub", BitRepMethod::FSub)
          .Case("Mod3", BitRepMethod::Mod3)
          .Default(BitRepMethod::DefaultBitRep);
    }
    return RepMethod;
  }

  BitFuscatorImpl(Function &F, FunctionAnalysisManager &FAM)
      : F(F), Builder(F.getContext()),
        BitRep(BitRepBase::createBitRep(Builder, getRepMethod())),
        DT(FAM.getResult<DominatorTreeAnalysis>(F)),
        DTU(DT, DomTreeUpdater::UpdateStrategy::Eager) {}

  bool run() {
    bool Changed = false;
    std::unordered_set<Instruction *> Set;
    for (auto &BB : F)
      for (auto &I : BB) {
        if (!I.getType()->isIntegerTy() && !isa<WithOverflowInst>(I))
          continue;

        bool Valid = true;
        for (auto &Op : I.operands())
          if (!Op->getType()->isIntegerTy()) {
            Valid = false;
            break;
          }

        if (Valid)
          Set.insert(&I);
      }

    for (auto &BB : F) {
      for (auto &I : BB) {
        if (!Set.count(&I))
          continue;

        Builder.SetInsertPoint(&I);
        if (auto *V = visit(I)) {
          I.replaceAllUsesWith(V);
          Changed = true;
        }
      }
    }

    // clean up
    while (true) {
      bool Simplified = false;
      for (auto &BB : F)
        Simplified |= SimplifyInstructionsInBlock(&BB);
      if (Simplified)
        Changed = true;
      else
        break;
    }

    if (verifyFunction(F, &errs()))
      report_fatal_error("BitFuscatorImpl: Function verification failed");

    return Changed;
  }
};

class FSubFuscator : public PassInfoMixin<FSubFuscator> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    if (BitFuscatorImpl(F, FAM).run()) {
      PreservedAnalyses PA;
      PA.preserve<DominatorTreeAnalysis>();
      return PA;
    }
    return PreservedAnalyses::all();
  }
};

void addFSubFuscatorPasses(FunctionPassManager &PM, OptimizationLevel Level) {
  if (Level != OptimizationLevel::O0) {
    PM.addPass(InstSimplifyPass());
    PM.addPass(InstCombinePass());
  }
  PM.addPass(FSubFuscator());
  if (Level != OptimizationLevel::O0) {
    // post clean up
    PM.addPass(EarlyCSEPass());
    PM.addPass(InstCombinePass());
  }
}
