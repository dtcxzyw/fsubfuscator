#!/bin/bash
set -euo pipefail
shopt -s inherit_errexit

mkdir build
cd build
export FSUBFUSCATOR_BITREP_OVERRIDE=Int1 
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=$(pwd)/../../build/fsubcc
cmake --build . -j
ctest -j
