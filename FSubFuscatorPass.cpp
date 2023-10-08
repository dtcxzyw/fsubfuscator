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
#include <algorithm>
#include <cassert>
#include <numeric>

cl::OptionCategory FsubFuscatorCategory("fsub fuscator options");

static Value *getConstantWithType(const Type *T, Constant *Val) {
  if (!T->isVectorTy())
    return Val;
  return ConstantVector::getSplat(dyn_cast<VectorType>(T)->getElementCount(),
                                  Val);
}

struct FSubBitRep final {
  static Type *getBitTy(IRBuilder<> &Builder) { return Builder.getFloatTy(); }
  static Constant *getBit0(IRBuilder<> &Builder) {
    return ConstantFP::get(getBitTy(Builder),
                           APFloat::getZero(APFloat::IEEEsingle(), true));
  }
  static Constant *getBit1(IRBuilder<> &Builder) {
    return ConstantFP::get(getBitTy(Builder),
                           APFloat::getZero(APFloat::IEEEsingle(), false));
  }

  // handle vector of i1
  static Value *convertToBit(IRBuilder<> &Builder, Value *V) {
    const auto *BitTy =
        dyn_cast<VectorType>(V->getType()->getWithNewType(getBitTy(Builder)));
    auto *Bit1 = getConstantWithType(BitTy, getBit1(Builder));
    auto *Bit0 = getConstantWithType(BitTy, getBit0(Builder));
    return Builder.CreateSelect(V, Bit1, Bit0);
  }
  // handle vector of bitTy
  static Value *convertFromBit(IRBuilder<> &Builder, Value *V) {
    auto *IntTy = V->getType()->getWithNewType(Builder.getInt32Ty());
    return Builder.CreateICmpSGE(Builder.CreateBitCast(V, IntTy),
                                 ConstantInt::getNullValue(IntTy));
  }

  static Value *bitNot(IRBuilder<> &Builder, Value *V) {
    return Builder.CreateFSub(
        getConstantWithType(V->getType(), getBit0(Builder)), V);
  }
  static Value *bitOr(IRBuilder<> &Builder, Value *V1, Value *V2) {
    return Builder.CreateFSub(V1, bitNot(Builder, V2));
  }
  static Value *bitAnd(IRBuilder<> &Builder, Value *V1, Value *V2) {
    return bitNot(Builder,
                  bitOr(Builder, bitNot(Builder, V1), bitNot(Builder, V2)));
  }
  static Value *bitXor(IRBuilder<> &Builder, Value *V1, Value *V2) {
    return bitOr(Builder, bitAnd(Builder, bitNot(Builder, V1), V2),
                 bitAnd(Builder, V1, bitNot(Builder, V2)));
  }
};

struct Int1BitRep final {
  static Type *getBitTy(IRBuilder<> &Builder) { return Builder.getInt1Ty(); }
  static Constant *getBit0(IRBuilder<> &Builder) { return Builder.getFalse(); }
  static Constant *getBit1(IRBuilder<> &Builder) { return Builder.getTrue(); }

  // handle vector of i1
  static Value *convertToBit(IRBuilder<> &Builder, Value *V) { return V; }
  // handle vector of bitTy
  static Value *convertFromBit(IRBuilder<> &Builder, Value *V) { return V; }

  static Value *bitNot(IRBuilder<> &Builder, Value *V) {
    return Builder.CreateNot(V);
  }
  static Value *bitOr(IRBuilder<> &Builder, Value *V1, Value *V2) {
    return Builder.CreateOr(V1, V2);
  }
  static Value *bitAnd(IRBuilder<> &Builder, Value *V1, Value *V2) {
    return Builder.CreateAnd(V1, V2);
  }
  static Value *bitXor(IRBuilder<> &Builder, Value *V1, Value *V2) {
    return Builder.CreateXor(V1, V2);
  }
};

class BitFuscatorImpl final : public InstVisitor<BitFuscatorImpl, Value *> {
  using BitRep = FSubBitRep;

  Function &F;
  IRBuilder<> Builder;

  Value *convertToBit(Value *V) {
    assert(!V->getType()->isVectorTy());
    auto *VT = VectorType::get(Builder.getInt1Ty(),
                               V->getType()->getScalarSizeInBits(),
                               /*Scalable*/ false);
    if (F.getParent()->getDataLayout().isBigEndian())
      V = Builder.CreateUnaryIntrinsic(Intrinsic::bswap, V);
    auto *Bits = Builder.CreateBitCast(V, VT);
    return BitRep::convertToBit(Builder, Bits);
  }
  Value *convertFromBit(Value *V, Type *DestTy) {
    assert(V->getType()->isVectorTy() && !DestTy->isVectorTy());
    auto *Bits = BitRep::convertFromBit(Builder, V);
    auto *Res = Builder.CreateBitCast(Bits, DestTy);
    if (F.getParent()->getDataLayout().isBigEndian())
      Res = Builder.CreateUnaryIntrinsic(Intrinsic::bswap, Res);
    return Res;
  }
  std::pair<Value *, Value *> fullAdder(Value *A, Value *B, Value *Carry) {
    auto *Xor = BitRep::bitXor(Builder, A, B);
    auto *Sum = BitRep::bitXor(Builder, Xor, Carry);
    auto *CarryOut = BitRep::bitOr(Builder, BitRep::bitAnd(Builder, Xor, Carry),
                                   BitRep::bitAnd(Builder, A, B));
    return {Sum, CarryOut};
  }
  std::pair<Value *, Value *> addWithOverflow(Value *V1, Value *V2, bool Sub) {
    auto *Op1 = convertToBit(V1);
    if (Sub)
      Op1 = BitRep::bitNot(Builder, Op1);
    auto *Op2 = convertToBit(V2);
    Value *Carry = Sub ? BitRep::getBit1(Builder) : BitRep::getBit0(Builder);

    auto Bits = V1->getType()->getScalarSizeInBits();
    Value *Res = PoisonValue::get(Op1->getType());
    for (int I = 0; I < Bits; ++I) {
      auto *A = Builder.CreateExtractElement(Op1, I);
      auto *B = Builder.CreateExtractElement(Op2, I);
      auto [Sum, CarryOut] = fullAdder(A, B, Carry);
      Res = Builder.CreateInsertElement(Res, Sum, I);
      Carry = CarryOut;
    }

    auto *ResVal = convertFromBit(Res, V1->getType());
    auto *CarryVal = BitRep::convertFromBit(Builder, Carry);
    return {ResVal, CarryVal};
  }

public:
  // rewrites
  Value *visitInstruction(Instruction &I) { return nullptr; }
  Value *visitAdd(BinaryOperator &I) {
    return addWithOverflow(I.getOperand(0), I.getOperand(1), /*Sub*/ false)
        .first;
  }
  Value *visitSub(BinaryOperator &I) {
    return addWithOverflow(I.getOperand(0), I.getOperand(1), /*Sub*/ true)
        .first;
  }
  Value *visitMul(BinaryOperator &I) { return nullptr; }
  Value *visitSDiv(BinaryOperator &I) { return nullptr; }
  Value *visitUDiv(BinaryOperator &I) { return nullptr; }
  Value *visitSRem(BinaryOperator &I) { return nullptr; }
  Value *visitURem(BinaryOperator &I) { return nullptr; }
  Value *visitShl(BinaryOperator &I) { return nullptr; }
  Value *visitAShr(BinaryOperator &I) { return nullptr; }
  Value *visitLShr(BinaryOperator &I) { return nullptr; }
  Value *visitAnd(BinaryOperator &I) {
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep::bitAnd(Builder, Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitOr(BinaryOperator &I) {
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep::bitOr(Builder, Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitXor(BinaryOperator &I) {
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Op1 = convertToBit(I.getOperand(1));
    auto *Res = BitRep::bitXor(Builder, Op0, Op1);
    return convertFromBit(Res, I.getType());
  }
  Value *visitCast(CastInst &I, bool NullOp1, ArrayRef<int> Mask) {
    auto *Op0 = convertToBit(I.getOperand(0));
    auto *Res = Builder.CreateShuffleVector(
        Op0,
        NullOp1 ? Constant::getNullValue(Op0->getType())
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

    return nullptr;
  }
  Value *visitSelect(SelectInst &I) {
    if (!I.getType()->isIntegerTy())
      return nullptr;
    return nullptr;
  }
  Value *visitPhi(PHINode &Phi) {
    if (!Phi.getType()->isIntegerTy())
      return nullptr;
    // TODO
    return nullptr;
  }
  Value *visitIntrinsicInst(IntrinsicInst &I) {
    // TODO: max/min/bitmanip/arithmetic with overflow
    return nullptr;
  }

  BitFuscatorImpl(Function &F, FunctionAnalysisManager &FAM)
      : F(F), Builder(F.getContext()) {}

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
