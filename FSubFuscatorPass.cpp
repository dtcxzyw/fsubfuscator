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

#include "FSubFuscatorPass.hpp"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Value.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>

cl::OptionCategory FsubFuscatorCategory("fsub fuscator options");

static cl::opt<bool>
    Verify("V",
           cl::desc("Use Int1BitRep instead of FSubBitRep for verification"),
           cl::cat(FsubFuscatorCategory));

static Value *getConstantWithType(const Type *T, Constant *Val) {
  if (!T->isVectorTy())
    return Val;
  return ConstantVector::getSplat(dyn_cast<VectorType>(T)->getElementCount(),
                                  Val);
}

struct BitRepBase {
  IRBuilder<> &Builder;

  explicit BitRepBase(IRBuilder<> &Builder) : Builder(Builder) {}
  virtual ~BitRepBase() = default;

  virtual Type *getBitTy() = 0;
  virtual Constant *getBit0() = 0;
  virtual Constant *getBit1() = 0;

  // handle vector of i1
  virtual Value *convertToBit(Value *V) = 0;
  // handle vector of bitTy
  virtual Value *convertFromBit(Value *V) = 0;
  virtual Value *bitNot(Value *V) = 0;
  virtual Value *bitOr(Value *V1, Value *V2) = 0;
  virtual Value *bitAnd(Value *V1, Value *V2) {
    return bitNot(bitOr(bitNot(V1), bitNot(V2)));
  }
  virtual Value *bitXor(Value *V1, Value *V2) {
    return bitOr(bitAnd(bitNot(V1), V2), bitAnd(V1, bitNot(V2)));
  }
};

struct FSubBitRep final : public BitRepBase {
  explicit FSubBitRep(IRBuilder<> &Builder) : BitRepBase(Builder) {}

  Type *getBitTy() override { return Builder.getFloatTy(); }
  Constant *getBit0() override {
    return ConstantFP::get(getBitTy(),
                           APFloat::getZero(APFloat::IEEEsingle(), true));
  }
  Constant *getBit1() override {
    return ConstantFP::get(getBitTy(),
                           APFloat::getZero(APFloat::IEEEsingle(), false));
  }

  // handle vector of i1
  Value *convertToBit(Value *V) override {
    const auto *BitTy =
        dyn_cast<VectorType>(V->getType()->getWithNewType(getBitTy()));
    auto *Bit1 = getConstantWithType(BitTy, getBit1());
    auto *Bit0 = getConstantWithType(BitTy, getBit0());
    return Builder.CreateSelect(V, Bit1, Bit0);
  }
  // handle vector of bitTy
  Value *convertFromBit(Value *V) override {
    auto *IntTy = V->getType()->getWithNewType(Builder.getInt32Ty());
    return Builder.CreateICmpSGE(Builder.CreateBitCast(V, IntTy),
                                 ConstantInt::getNullValue(IntTy));
  }

  Value *bitNot(Value *V) override {
    return Builder.CreateFSub(getConstantWithType(V->getType(), getBit0()), V);
  }
  Value *bitOr(Value *V1, Value *V2) override {
    return Builder.CreateFSub(V1, bitNot(V2));
  }
};

struct Int1BitRep final : public BitRepBase {
  explicit Int1BitRep(IRBuilder<> &Builder) : BitRepBase(Builder) {}

  Type *getBitTy() override { return Builder.getInt1Ty(); }
  Constant *getBit0() override { return Builder.getFalse(); }
  Constant *getBit1() override { return Builder.getTrue(); }

  // handle vector of i1
  Value *convertToBit(Value *V) override { return V; }
  // handle vector of bitTy
  Value *convertFromBit(Value *V) override { return V; }

  Value *bitNot(Value *V) override { return Builder.CreateNot(V); }
  Value *bitOr(Value *V1, Value *V2) override {
    return Builder.CreateOr(V1, V2);
  }
};

class BitFuscatorImpl final : public InstVisitor<BitFuscatorImpl, Value *> {
  Function &F;
  IRBuilder<> Builder;
  std::unique_ptr<BitRepBase> BitRep;

  Value *convertToBit(Value *V) {
    assert(!V->getType()->isVectorTy());
    auto *VT = VectorType::get(Builder.getInt1Ty(),
                               V->getType()->getScalarSizeInBits(),
                               /*Scalable*/ false);
    if (F.getParent()->getDataLayout().isBigEndian())
      V = Builder.CreateUnaryIntrinsic(Intrinsic::bswap, V);
    auto *Bits = Builder.CreateBitCast(V, VT);
    return BitRep->convertToBit(Bits);
  }
  Value *convertFromBit(Value *V, Type *DestTy) {
    assert(V->getType()->isVectorTy() && !DestTy->isVectorTy());
    auto *Bits = BitRep->convertFromBit(V);
    auto *Res = Builder.CreateBitCast(Bits, DestTy);
    if (F.getParent()->getDataLayout().isBigEndian())
      Res = Builder.CreateUnaryIntrinsic(Intrinsic::bswap, Res);
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
    std::iota(Mask.begin(), Mask.begin(), 1);
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
    auto *Post = SplitBlock(Block, InsertPt);
    auto *Header = BasicBlock::Create(F.getContext(), "mul.header", &F);
    auto *Body = BasicBlock::Create(F.getContext(), "mul.body", &F);
    auto *Zero = getConstantWithType(Op1->getType(), BitRep->getBit0());

    // Pre
    Block->getTerminator()->setSuccessor(0, Header);
    // Header
    Builder.SetInsertPoint(Header);
    auto *Res = Builder.CreatePHI(Op1->getType(), 2);
    auto *A = Builder.CreatePHI(Op1->getType(), 2);
    auto *B = Builder.CreatePHI(Op1->getType(), 2);
    auto *Cond = nonZero(B);
    Builder.CreateCondBr(Cond, Body, Post);
    // Body
    Builder.SetInsertPoint(Body);
    auto *IsOdd = BitRep->convertFromBit(
        Builder.CreateExtractElement(B, Builder.getInt32(0)));
    auto *Add = Builder.CreateSelect(IsOdd, A, Zero);
    auto *Sum =
        addWithOverflowBits(Res, Add, /*Sub*/ false, /*Unsigned*/ true, Bits)
            .first;
    auto *NextA =
        addWithOverflowBits(A, A, /*Sub*/ false, /*Unsigned*/ true, Bits).first;
    auto *NextB = lshr1(B);
    Builder.CreateBr(Header);
    // Phi nodes
    Res->addIncoming(Zero, Block);
    A->addIncoming(Op1, Block);
    B->addIncoming(Op2, Block);

    Res->addIncoming(Sum, Body);
    A->addIncoming(NextA, Body);
    B->addIncoming(NextB, Body);

    // Post
    Builder.SetInsertPoint(Post, Post->getFirstInsertionPt());
    return convertFromBit(Res, V1->getType());
  }

public:
  // rewrites
  Value *visitInstruction(Instruction &I) { return nullptr; }
  Value *visitAdd(BinaryOperator &I) {
    return addWithOverflow(I.getOperand(0), I.getOperand(1), /*Sub*/ false,
                           /*Unsigned*/ true)
        .first;
  }
  Value *visitSub(BinaryOperator &I) {
    return addWithOverflow(I.getOperand(0), I.getOperand(1), /*Sub*/ true,
                           /*Unsigned*/ true)
        .first;
  }
  Value *visitMul(BinaryOperator &I) {
    return mult(I.getOperand(0), I.getOperand(1));
  }
  Value *visitSDiv(BinaryOperator &I) { return nullptr; }
  Value *visitUDiv(BinaryOperator &I) { return nullptr; }
  Value *visitSRem(BinaryOperator &I) { return nullptr; }
  Value *visitURem(BinaryOperator &I) { return nullptr; }
  template <typename ShiftOnce>
  Value *visitShift(BinaryOperator &I, ShiftOnce Func) {
    auto *Src = convertToBit(I.getOperand(0));
    auto *ShAmt = Builder.CreateFreeze(I.getOperand(1));

    auto *Block = Builder.GetInsertBlock();
    auto InsertPt = std::next(Builder.GetInsertPoint());
    auto *Post = SplitBlock(Block, InsertPt);
    auto *Header = BasicBlock::Create(F.getContext(), "shift.header", &F);
    auto *Body = BasicBlock::Create(F.getContext(), "shift.body", &F);

    // Pre
    Block->getTerminator()->setSuccessor(0, Header);
    // Header
    Builder.SetInsertPoint(Header);
    auto *IndVar = Builder.CreatePHI(I.getType(), 2);
    auto *Res = Builder.CreatePHI(Src->getType(), 2);
    auto *Cond = Builder.CreateICmpNE(IndVar, ShAmt);
    Builder.CreateCondBr(Cond, Body, Post);
    // Body
    Builder.SetInsertPoint(Body);
    auto *NextIndVar =
        Builder.CreateAdd(IndVar, ConstantInt::get(IndVar->getType(), 1));
    auto *NextRes = Func(Res);
    Builder.CreateBr(Header);
    // Phi nodes
    IndVar->addIncoming(ConstantInt::getNullValue(IndVar->getType()), Block);
    Res->addIncoming(Src, Block);

    IndVar->addIncoming(NextIndVar, Body);
    Res->addIncoming(NextRes, Body);

    // Post
    Builder.SetInsertPoint(Post, Post->getFirstInsertionPt());
    return convertFromBit(Res, I.getType());
  }
  Value *visitShl(BinaryOperator &I) {
    return visitShift(I, [&](Value *V) { return shl1(V); });
  }
  Value *visitAShr(BinaryOperator &I) {
    return visitShift(I, [&](Value *V) { return ashr1(V); });
  }
  Value *visitLShr(BinaryOperator &I) {
    return visitShift(I, [&](Value *V) { return lshr1(V); });
  }
  Value *visitAnd(BinaryOperator &I) {
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep->bitAnd(Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitOr(BinaryOperator &I) {
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep->bitOr(Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitXor(BinaryOperator &I) {
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep->bitXor(Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitCast(CastInst &I, bool NullOp1, ArrayRef<int> Mask) {
    auto *Op0 = convertToBit(I.getOperand(0));
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
    return visitCast(I, /*NullOp1*/ false, Mask);
  }
  Value *visitZExt(ZExtInst &I) {
    auto DestBits = I.getType()->getScalarSizeInBits();
    auto SrcBits = I.getOperand(0)->getType()->getScalarSizeInBits();
    SmallVector<int, 64> Mask(DestBits);
    std::iota(Mask.begin(), Mask.begin() + SrcBits, 0);
    std::fill(Mask.begin() + SrcBits, Mask.end(), SrcBits);
    return visitCast(I, /*NullOp1*/ true, Mask);
  }
  Value *visitSExt(SExtInst &I) {
    auto DestBits = I.getType()->getScalarSizeInBits();
    auto SrcBits = I.getOperand(0)->getType()->getScalarSizeInBits();
    SmallVector<int, 64> Mask(DestBits);
    std::iota(Mask.begin(), Mask.begin() + SrcBits, 0);
    std::fill(Mask.begin() + SrcBits, Mask.end(), SrcBits - 1);
    return visitCast(I, /*NullOp1*/ false, Mask);
  }
  Value *visitICmp(ICmpInst &I) {
    if (!I.getType()->isIntegerTy())
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
        /*Sub*/ true, /*Unsigned*/ I.isEquality() || I.isUnsigned());
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
    // TODO
    return nullptr;
  }
  Value *visitIntrinsicInst(IntrinsicInst &I) {
    // TODO: max/min/bitmanip/funnel shift
    switch (I.getIntrinsicID()) {
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::usub_with_overflow: {
      auto *LHS = Builder.CreateFreeze(I.getOperand(0));
      auto *RHS = Builder.CreateFreeze(I.getOperand(1));
      auto [Res, Overflow] = addWithOverflow(LHS, RHS,
                                             /*Sub*/ I.getIntrinsicID() ==
                                                 Intrinsic::usub_with_overflow,
                                             /*Unsigned*/ true);
      auto *Pair =
          Builder.CreateInsertValue(UndefValue::get(I.getType()), Res, {0});
      return Builder.CreateInsertValue(Pair, Overflow, {1});
    }
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::ssub_with_overflow: {
      auto *LHS = Builder.CreateFreeze(I.getOperand(0));
      auto *RHS = Builder.CreateFreeze(I.getOperand(1));
      auto [Res, Overflow] = addWithOverflow(LHS, RHS,
                                             /*Sub*/ I.getIntrinsicID() ==
                                                 Intrinsic::ssub_with_overflow,
                                             /*Unsigned*/ false);
      auto *Pair =
          Builder.CreateInsertValue(UndefValue::get(I.getType()), Res, {0});
      return Builder.CreateInsertValue(Pair, Overflow, {1});
    }
    case Intrinsic::ctpop: {
      auto *Bits = BitRep->convertFromBit(convertToBit(I.getOperand(0)));
      auto *ZExt = Builder.CreateZExt(
          Bits, Bits->getType()->getWithNewType(I.getType()));
      // FIXME: use BitRep
      return Builder.CreateAddReduce(ZExt);
    }
    default:
      return nullptr;
    }
  }

  static std::unique_ptr<BitRepBase> getBitRep(IRBuilder<> &Builder) {
    if (Verify)
      return std::make_unique<Int1BitRep>(Builder);
    return std::make_unique<FSubBitRep>(Builder);
  }

  BitFuscatorImpl(Function &F, FunctionAnalysisManager &FAM)
      : F(F), Builder(F.getContext()), BitRep(getBitRep(Builder)) {}

  bool run() {
    bool Changed = false;
    for (auto &BB : F) {
      for (auto &I : BB) {
        Builder.SetInsertPoint(&I);
        if (auto *V = visit(I)) {
          I.replaceAllUsesWith(V);
          Changed = true;
        }
      }
    }

    // clean up int-bitvec-int converts

    return Changed;
  }
};

class FSubFuscator : public PassInfoMixin<FSubFuscator> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    if (BitFuscatorImpl(F, FAM).run())
      return PreservedAnalyses::none();
    return PreservedAnalyses::all();
  }
};

void addFSubFuscatorPasses(FunctionPassManager &PM, OptimizationLevel Level) {
  PM.addPass(FSubFuscator());
  if (Level != OptimizationLevel::O0) {
    // post clean up
  }
}
