#!/bin/sh
# v5: build the research compiler from linux/work/llvm-project (branch
# claude/v5-unj-fp-fix = xreg fc34b3040 + the unj-auto FP-phi crash fix) into
# linux/work/llvm-build, with the same CMake options as LLVM-regsw/llvm-build.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$HERE/work"
B="$W/llvm-build"
mkdir -p "$B"
cd "$B"
[ -f build.ninja ] || cmake -G Ninja "$W/llvm-project/llvm" \
    -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
    -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD=RISCV \
    -DLLVM_DEFAULT_TARGET_TRIPLE=riscv64-unknown-elf \
    -DLLVM_CCACHE_BUILD=ON -DLLVM_CCACHE_DIR="$HERE/../.ccache" \
    -DLLVM_PARALLEL_LINK_JOBS=4 > cmake.log 2>&1
ninja -j"${J:-16}" clang lld llc opt llvm-mc llvm-objdump llvm-ar llvm-ranlib llvm-size \
    llvm-symbolizer llvm-profdata > ninja.log 2>&1
echo "llvm: $B/bin/clang"
