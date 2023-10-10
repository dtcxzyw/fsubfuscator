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

Value *getConstantWithType(const Type *T, Constant *Val) {
  if (!T->isVectorTy())
    return Val;
  return ConstantVector::getSplat(dyn_cast<VectorType>(T)->getElementCount(),
                                  Val);
}

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
  Value *bitAnd(Value *V1, Value *V2) override {
    return Builder.CreateAnd(V1, V2);
  }
  Value *bitXor(Value *V1, Value *V2) override {
    return Builder.CreateXor(V1, V2);
  }
};

struct InvInt1BitRep final : public BitRepBase {
  explicit InvInt1BitRep(IRBuilder<> &Builder) : BitRepBase(Builder) {}

  Type *getBitTy() override { return Builder.getInt1Ty(); }
  Constant *getBit0() override { return Builder.getTrue(); }
  Constant *getBit1() override { return Builder.getFalse(); }

  // handle vector of i1
  Value *convertToBit(Value *V) override { return Builder.CreateNot(V); }
  // handle vector of bitTy
  Value *convertFromBit(Value *V) override { return Builder.CreateNot(V); }

  Value *bitNot(Value *V) override { return Builder.CreateNot(V); }
  Value *bitOr(Value *V1, Value *V2) override {
    return Builder.CreateAnd(V1, V2);
  }
  Value *bitAnd(Value *V1, Value *V2) override {
    return Builder.CreateOr(V1, V2);
  }
  Value *bitXor(Value *V1, Value *V2) override {
    return Builder.CreateICmpEQ(V1, V2);
  }
};

std::unique_ptr<BitRepBase> BitRepBase::createBitRep(IRBuilder<> &Builder,
                                                     BitRepMethod Method) {
  switch (Method) {
  case FSub:
    return std::make_unique<FSubBitRep>(Builder);
  case Int1:
    return std::make_unique<Int1BitRep>(Builder);
  case InvInt1:
    return std::make_unique<InvInt1BitRep>(Builder);
  default:
    llvm_unreachable("Unexpected bit representation method");
  }
}
