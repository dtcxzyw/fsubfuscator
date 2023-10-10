#!/usr/bin/env python3

import os
import sys
import tqdm
import subprocess
import shutil
import psutil
from concurrent.futures import ThreadPoolExecutor

# Usage: csmith_fuzz.py <tmp_dir> <mode> <number of tests> <concurrency>
# Example1: csmith_fuzz.py csmith c 100 16
# Example2: csmith_fuzz.py csmith c++ 100 16

tmp_dir = sys.argv[1]
mode = sys.argv[2]
extra_args = {
    'c': '',
    'c++': '--lang-cpp --cpp11',
}[mode]
compiler = {
    'c': './fsubcc',
    'c++': './fsub++',
}[mode]
reference_compiler_gcc = {
    'c': 'gcc',
    'c++': 'g++',
}[mode]
reference_compiler_clang = {
    'c': 'clang',
    'c++': 'clang++',
}[mode]
num_tests = int(sys.argv[3])
concurrency = int(sys.argv[4])

# Create tmp dir
if os.path.exists(tmp_dir):
    shutil.rmtree(tmp_dir)
os.makedirs(tmp_dir)

csmith_args = "--no-volatiles --no-bitfields --max-funcs 1 --max-block-depth 1 --max-block-size 1 --max-expr-complexity 1"


def try_remove(filename):
    if os.path.exists(filename):
        os.remove(filename)


def cleanup(id):
    try_remove(f"{tmp_dir}/{id}.c")
    try_remove(f"{tmp_dir}/{id}.out_gcc")
    try_remove(f"{tmp_dir}/{id}.out_clang")
    try_remove(f"{tmp_dir}/{id}.out")


def csmith_test(id):
    ret = os.system(f'csmith {csmith_args} {extra_args} -o {tmp_dir}/{id}.c')
    if ret != 0:
        return None

    ret = os.system(
        f'{reference_compiler_gcc} -I/usr/include/csmith -w -O3 -DNDEBUG {tmp_dir}/{id}.c -o {tmp_dir}/{id}.out_gcc')
    if ret != 0:
        cleanup(id)
        return None
    ret = os.system(
        f'{reference_compiler_clang} -I/usr/include/csmith -w -O3 -DNDEBUG {tmp_dir}/{id}.c -o {tmp_dir}/{id}.out_clang')
    if ret != 0:
        cleanup(id)
        return None

    ret = os.system(
        f'{compiler} -I/usr/include/csmith -w -O3 -fno-fast-math -fno-unsafe-math-optimizations -DNDEBUG {tmp_dir}/{id}.c -o {tmp_dir}/{id}.out 2>/dev/null')
    if ret != 0:
        return False

    try:
        ref_output_gcc = subprocess.check_output(
            f"{tmp_dir}/{id}.out_gcc", timeout=10.0)
    except subprocess.TimeoutExpired:
        cleanup(id)
        return None

    try:
        ref_output_clang = subprocess.check_output(
            f"{tmp_dir}/{id}.out_clang", timeout=10.0)
    except subprocess.TimeoutExpired:
        cleanup(id)
        return None

    if ref_output_gcc != ref_output_clang:
        cleanup(id)
        return None

    try:
        output = subprocess.check_output(f"{tmp_dir}/{id}.out", timeout=60.0)
        if ref_output_gcc != output:
            return False
        cleanup(id)
        return True
    except subprocess.TimeoutExpired:
        return False


L = list(range(num_tests))
pbar = tqdm.tqdm(L)
error_count = 0
skipped_count = 0

try:
    with ThreadPoolExecutor(max_workers=concurrency) as p:
        for res in p.map(csmith_test, L):
            if res is not None:
                error_count += 0 if res else 1
            else:
                skipped_count += 1

            pbar.update(1)
            pbar.set_description("Failed: {} Skipped: {}".format(
                error_count, skipped_count))
except KeyboardInterrupt:
    pass

process = psutil.Process()
for proc in process.children(recursive=True):
    proc.kill()
