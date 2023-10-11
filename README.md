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

After (with `-mcpu=skylake`):
```
.LCPI0_0:
        .long   1                               # 0x1
        .long   2                               # 0x2
        .long   4                               # 0x4
        .long   8                               # 0x8
.LCPI0_1:
        .long   0x80000000                      # float -0
add:                                    # @add
        vmovd   xmm0, edi
        vpbroadcastd    xmm0, xmm0
        vmovdqa xmm1, xmmword ptr [rip + .LCPI0_0] # xmm1 = [1,2,4,8]
        vpand   xmm0, xmm0, xmm1
        vpxor   xmm2, xmm2, xmm2
        vpcmpeqd        xmm3, xmm0, xmm2
        vpbroadcastd    xmm0, dword ptr [rip + .LCPI0_1] # xmm0 = [-0.0E+0,-0.0E+0,-0.0E+0,-0.0E+0]
        vpand   xmm3, xmm3, xmm0
        vmovd   xmm4, esi
        vpbroadcastd    xmm4, xmm4
        vpand   xmm1, xmm4, xmm1
        vpcmpeqd        xmm1, xmm1, xmm2
        vpand   xmm2, xmm1, xmm0
        vpxor   xmm1, xmm3, xmm0
        vsubss  xmm4, xmm2, xmm3
        vsubss  xmm5, xmm3, xmm2
        vxorps  xmm5, xmm5, xmm0
        vsubss  xmm6, xmm1, xmm2
        vshufps xmm1, xmm3, xmm3, 245           # xmm1 = xmm3[1,1,3,3]
        vshufps xmm7, xmm2, xmm2, 245           # xmm7 = xmm2[1,1,3,3]
        vxorps  xmm8, xmm1, xmm0
        vsubss  xmm9, xmm7, xmm1
        vsubss  xmm1, xmm1, xmm7
        vxorps  xmm1, xmm1, xmm0
        vsubss  xmm9, xmm1, xmm9
        vbroadcastss    xmm1, xmm9
        vxorps  xmm10, xmm1, xmm0
        vsubss  xmm7, xmm8, xmm7
        vshufps xmm8, xmm3, xmm3, 78            # xmm8 = xmm3[2,3,0,1]
        vshufps xmm11, xmm2, xmm2, 78           # xmm11 = xmm2[2,3,0,1]
        vxorps  xmm12, xmm8, xmm0
        vsubss  xmm13, xmm11, xmm8
        vsubss  xmm8, xmm8, xmm11
        vxorps  xmm8, xmm8, xmm0
        vsubss  xmm8, xmm8, xmm13
        vxorps  xmm13, xmm8, xmm0
        vsubss  xmm11, xmm12, xmm11
        vshufps xmm3, xmm3, xmm3, 255           # xmm3 = xmm3[3,3,3,3]
        vshufps xmm2, xmm2, xmm2, 255           # xmm2 = xmm2[3,3,3,3]
        vsubss  xmm12, xmm2, xmm3
        vsubss  xmm2, xmm3, xmm2
        vxorps  xmm2, xmm2, xmm0
        vsubss  xmm2, xmm2, xmm12
        vsubss  xmm3, xmm5, xmm4
        vxorps  xmm4, xmm4, xmm4
        vsubss  xmm5, xmm4, xmm3
        vxorps  xmm5, xmm5, xmm0
        vsubss  xmm5, xmm5, xmm6
        vsubss  xmm6, xmm9, xmm5
        vxorps  xmm6, xmm6, xmm0
        vsubss  xmm9, xmm10, xmm5
        vxorps  xmm9, xmm9, xmm0
        vsubss  xmm7, xmm9, xmm7
        vsubss  xmm9, xmm8, xmm7
        vxorps  xmm9, xmm9, xmm0
        vsubss  xmm10, xmm13, xmm7
        vxorps  xmm10, xmm10, xmm0
        vsubss  xmm10, xmm10, xmm11
        vshufps xmm5, xmm5, xmm7, 0             # xmm5 = xmm5[0,0],xmm7[0,0]
        vinsertps       xmm5, xmm5, xmm10, 48   # xmm5 = xmm5[0,1,2],xmm10[0]
        vinsertps       xmm1, xmm1, xmm8, 32    # xmm1 = xmm1[0,1],xmm8[0],xmm1[3]
        vinsertps       xmm1, xmm1, xmm2, 48    # xmm1 = xmm1[0,1,2],xmm2[0]
        vsubps  xmm1, xmm5, xmm1
        vaddss  xmm4, xmm3, xmm4
        vblendps        xmm1, xmm1, xmm4, 1             # xmm1 = xmm4[0],xmm1[1,2,3]
        vsubss  xmm2, xmm2, xmm10
        vxorps  xmm0, xmm2, xmm0
        vunpcklps       xmm2, xmm3, xmm6        # xmm2 = xmm3[0],xmm6[0],xmm3[1],xmm6[1]
        vmovlhps        xmm2, xmm2, xmm9                # xmm2 = xmm2[0],xmm9[0]
        vinsertps       xmm0, xmm2, xmm0, 48    # xmm0 = xmm2[0,1,2],xmm0[0]
        vsubps  xmm0, xmm0, xmm1
        vmovmskps       eax, xmm0
        xor     eax, 15
        ret
```

## Limitations

+ It doesn't support bitfield since some integer operations with uncommon bitwidths are not supported by CodeGen.

+ It doesn't support vectorized code yet. Currently it rewrites IR before vectorization.

+ It doesn't work with `-ffast-math` since clang will ignore the sign of floating point zeros. See also https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior.

## License
This repository is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
