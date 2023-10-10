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
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TypeSize.h>
#include <cstdint>
#include <gtest/gtest.h>

class BinRepTest : public ::testing::Test {
protected:
  BinRepTest() : Builder{Context} {}

  LLVMContext Context;
  IRBuilder<> Builder;
};

template <typename Callable>
void testTruthTable(Value *FalseV, Value *TrueV, Callable Func,
                    uint32_t Table) {
  auto V00 = Func(FalseV, FalseV);
  ASSERT_TRUE(isa<Constant>(V00));
  ASSERT_EQ(V00, (Table & 1) ? TrueV : FalseV);
  auto V01 = Func(FalseV, TrueV);
  ASSERT_TRUE(isa<Constant>(V01));
  ASSERT_EQ(V01, (Table & 2) ? TrueV : FalseV);
  auto V10 = Func(TrueV, FalseV);
  ASSERT_TRUE(isa<Constant>(V10));
  ASSERT_EQ(V10, (Table & 4) ? TrueV : FalseV);
  auto V11 = Func(TrueV, TrueV);
  ASSERT_TRUE(isa<Constant>(V11));
  ASSERT_EQ(V11, (Table & 8) ? TrueV : FalseV);
}

static void testBitRep(IRBuilder<> &Builder, BitRepMethod Method) {
  auto BitRep = BitRepBase::createBitRep(Builder, Method);
  auto *BitTy = BitRep->getBitTy();
  auto *Bit0 = BitRep->getBit0();
  ASSERT_EQ(Bit0->getType(), BitTy);
  auto *Bit1 = BitRep->getBit1();
  ASSERT_EQ(Bit1->getType(), BitTy);
  ASSERT_NE(Bit0, Bit1);
  auto *V1I1 = VectorType::get(Builder.getInt1Ty(), ElementCount::getFixed(1));
  auto *V1Bit = VectorType::get(BitTy, ElementCount::getFixed(1));
  auto *Bit0Vec = getConstantWithType(V1Bit, Bit0);
  auto *Bit1Vec = getConstantWithType(V1Bit, Bit1);
  auto *V0 = BitRep->convertToBit(ConstantInt::getFalse(V1I1));
  ASSERT_TRUE(isa<Constant>(V0));
  ASSERT_EQ(V0, Bit0Vec);
  auto *V1 = BitRep->convertToBit(ConstantInt::getTrue(V1I1));
  ASSERT_TRUE(isa<Constant>(V1));
  ASSERT_EQ(V1, Bit1Vec);
  auto *W0 = BitRep->convertFromBit(V0);
  auto *W1 = BitRep->convertFromBit(V1);
  ASSERT_TRUE(isa<Constant>(W0));
  ASSERT_EQ(W0, ConstantInt::getFalse(V1I1));
  ASSERT_TRUE(isa<Constant>(W1));
  ASSERT_EQ(W1, ConstantInt::getTrue(V1I1));

  auto *N0 = BitRep->bitNot(V0);
  ASSERT_TRUE(isa<Constant>(N0));
  ASSERT_EQ(N0, Bit1Vec);
  auto *N1 = BitRep->bitNot(V1);
  ASSERT_TRUE(isa<Constant>(N1));
  ASSERT_EQ(N1, Bit0Vec);

  testTruthTable(
      V0, V1, [&](Value *A, Value *B) { return BitRep->bitAnd(A, B); }, 0b1000);
  testTruthTable(
      V0, V1, [&](Value *A, Value *B) { return BitRep->bitOr(A, B); }, 0b1110);
  testTruthTable(
      V0, V1, [&](Value *A, Value *B) { return BitRep->bitXor(A, B); }, 0b0110);
}

TEST_F(BinRepTest, MethodInt1) { testBitRep(Builder, BitRepMethod::Int1); }

TEST_F(BinRepTest, MethodInvInt1) {
  testBitRep(Builder, BitRepMethod::InvInt1);
}

TEST_F(BinRepTest, MethodFSub) { testBitRep(Builder, BitRepMethod::FSub); }

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  InitLLVM Init{argc, argv};
  return RUN_ALL_TESTS();
}
