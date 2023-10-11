# FSubfuscator

[![Build](https://github.com/dtcxzyw/fsubfuscator/actions/workflows/build.yml/badge.svg)](https://github.com/dtcxzyw/fsubfuscator/actions/workflows/build.yml)

An obfuscator inspired by [Subtraction Is Functionally Complete](https://orlp.net/blog/subtraction-is-functionally-complete/).

## Overview

The FSubfuscator rewrites most of integer arithmetic operations into a bunch of `fsub`s. It is implemented as a LLVM pass and can be used as a standalone tool or a compiler wrapper.

It supports C/C++/Rust and other programming languages that use LLVM as the backend.

## Building
### Prerequisites

+ Linux
+ Compiler with C++17 support
+ CMake 3.20 or higher
+ Ninja
+ Recent versions of LLVM
+ alive2 (optional for test)
+ gtest (optional for test)
+ csmith (optional for fuzzing)
  
```
# Install dependencies with apt
wget -O- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
sudo add-apt-repository "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy main"
sudo apt-get update
sudo apt-get install csmith cmake z3 re2c ninja-build clang-18 llvm-18-dev libgtest-dev
```

### Build and Test
```
git clone https://github.com/dtcxzyw/fsubfuscator.git
cd fsubfuscator
mkdir build
cd build

cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DFSUBFUSCATOR_ENABLE_TESTS=ON ..
cmake --build . -j
ctest # optional
```
## Usage
```
# Standalone
./fsubfuscator test.ll -S -o out.ll
# With wrapper
# Note: O0 pipeline is not supported yet.
./fsubcc -O3 test.c
./fsub++ -O3 test.cpp
./opt-fsub.sh -O3 test.ll -S -o out.ll
# Use wrapper in your project with Make
export CC=<path to build>/fsubcc
export CXX=<path to build>/fsub++
configure && make ...
# Use wrapper in your project with CMake
cmake -DCMAKE_C_COMPILER=<path to build>/fsubcc -DCMAKE_CXX_COMPILER=<path to build>/fsub++ ...
# To use other bit representation instead of fsub
./fsubfuscator -bitrep=<FSub|Int1|InvInt1> test.ll -S -o out.ll
# or use the environment variable (highest priority) to pass the parameter to clang
export FSUBFUSCATOR_BITREP_OVERRIDE=<FSub|Int1|InvInt1>
./fsubcc ...
```

## Extending fsubfuscator
### Add a new bit representation
+ Implement the new bit representation in `BitRep.cpp`. It should implement `BitRepBase` interface.
+ Add a enum value to `BitRepMethod` in `BitRep.hpp`
+ Add a switch case to `BitRepBase::createBitRep` in `BitRep.cpp`
+ Add a unittest in `BitRepTest.cpp`
+ Add a test in `CMakeLists.txt`

### Handle new instructions
+ Implement the rewriting logic in `FSubFuscatorPass.cpp`
+ If it creates new blocks, make sure that the DomTree is updated correctly before calling `convertFromBit`
+ Add some tests to `test.ll` and `test.cpp`

## Example


X86-64 Assembly for int4 a + b:

Before:
```
add:                                    # @add
        lea     eax, [rdi + rsi]
        ret
```

After:
```
.LCPI0_0:
        .long   1                               # 0x1
        .long   2                               # 0x2
        .long   4                               # 0x4
        .long   8                               # 0x8
.LCPI0_1:
        .long   0x80000000                      # float -0
        .long   0x80000000                      # float -0
        .long   0x80000000                      # float -0
        .long   0x80000000                      # float -0
add:                                    # @add
        movd    xmm0, edi
        pshufd  xmm6, xmm0, 0                   # xmm6 = xmm0[0,0,0,0]
        movdqa  xmm1, xmmword ptr [rip + .LCPI0_0] # xmm1 = [1,2,4,8]
        pand    xmm6, xmm1
        pxor    xmm2, xmm2
        pcmpeqd xmm6, xmm2
        movdqa  xmm0, xmmword ptr [rip + .LCPI0_1] # xmm0 = [-0.0E+0,-0.0E+0,-0.0E+0,-0.0E+0]
        pand    xmm6, xmm0
        movd    xmm3, esi
        pshufd  xmm7, xmm3, 0                   # xmm7 = xmm3[0,0,0,0]
        pand    xmm7, xmm1
        pcmpeqd xmm7, xmm2
        pand    xmm7, xmm0
        movdqa  xmm5, xmm7
        subss   xmm5, xmm6
        movaps  xmm1, xmm6
        pshufd  xmm2, xmm6, 85                  # xmm2 = xmm6[1,1,1,1]
        pshufd  xmm3, xmm6, 238                 # xmm3 = xmm6[2,3,2,3]
        pshufd  xmm4, xmm6, 255                 # xmm4 = xmm6[3,3,3,3]
        movdqa  xmm12, xmm6
        pxor    xmm12, xmm0
        subss   xmm1, xmm7
        xorps   xmm1, xmm0
        subss   xmm12, xmm7
        pshufd  xmm8, xmm7, 85                  # xmm8 = xmm7[1,1,1,1]
        movdqa  xmm10, xmm2
        pxor    xmm10, xmm0
        movdqa  xmm6, xmm8
        subss   xmm6, xmm2
        subss   xmm2, xmm8
        xorps   xmm2, xmm0
        subss   xmm2, xmm6
        movaps  xmm6, xmm2
        xorps   xmm6, xmm0
        subss   xmm10, xmm8
        pshufd  xmm9, xmm7, 238                 # xmm9 = xmm7[2,3,2,3]
        movdqa  xmm11, xmm3
        pxor    xmm11, xmm0
        movdqa  xmm8, xmm9
        subss   xmm8, xmm3
        subss   xmm3, xmm9
        xorps   xmm3, xmm0
        subss   xmm3, xmm8
        movaps  xmm8, xmm3
        xorps   xmm8, xmm0
        subss   xmm11, xmm9
        pshufd  xmm7, xmm7, 255                 # xmm7 = xmm7[3,3,3,3]
        movdqa  xmm9, xmm7
        subss   xmm9, xmm4
        subss   xmm4, xmm7
        xorps   xmm4, xmm0
        subss   xmm4, xmm9
        subss   xmm1, xmm5
        xorps   xmm7, xmm7
        xorps   xmm9, xmm9
        subss   xmm9, xmm1
        xorps   xmm9, xmm0
        subss   xmm9, xmm12
        movaps  xmm5, xmm2
        subss   xmm5, xmm9
        xorps   xmm5, xmm0
        subss   xmm6, xmm9
        xorps   xmm6, xmm0
        subss   xmm6, xmm10
        movaps  xmm10, xmm3
        subss   xmm10, xmm6
        xorps   xmm10, xmm0
        subss   xmm8, xmm6
        xorps   xmm8, xmm0
        subss   xmm8, xmm11
        unpcklps        xmm6, xmm8                      # xmm6 = xmm6[0],xmm8[0],xmm6[1],xmm8[1]
        addss   xmm7, xmm1
        movlhps xmm5, xmm1                      # xmm5 = xmm5[0],xmm1[0]
        unpcklps        xmm1, xmm9                      # xmm1 = xmm1[0],xmm9[0],xmm1[1],xmm9[1]
        movlhps xmm1, xmm6                      # xmm1 = xmm1[0],xmm6[0]
        unpcklps        xmm3, xmm4                      # xmm3 = xmm3[0],xmm4[0],xmm3[1],xmm4[1]
        shufps  xmm2, xmm3, 64                  # xmm2 = xmm2[0,0],xmm3[0,1]
        subps   xmm1, xmm2
        movss   xmm1, xmm7                      # xmm1 = xmm7[0],xmm1[1,2,3]
        subss   xmm4, xmm8
        xorps   xmm4, xmm0
        movlhps xmm4, xmm10                     # xmm4 = xmm4[0],xmm10[0]
        shufps  xmm5, xmm4, 34                  # xmm5 = xmm5[2,0],xmm4[2,0]
        subps   xmm5, xmm1
        movmskps        eax, xmm5
        xor     eax, 15
        ret
```

## Limitations

+ It doesn't support bitfield since some integer operations with uncommon bitwidths are not supported by CodeGen.

+ It doesn't support vectorized code yet. Currently it rewrites IR before vectorization.

## License
This repository is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
