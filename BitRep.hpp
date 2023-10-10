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

#pragma once
#include <llvm/IR/IRBuilder.h>
using namespace llvm;

enum BitRepMethod {
  FSub,
  Int1,
  InvInt1,

  DefaultBitRep = FSub,
};

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
  static std::unique_ptr<BitRepBase> createBitRep(IRBuilder<> &Builder,
                                                  BitRepMethod Method);
};
Value *getConstantWithType(const Type *T, Constant *Val);
