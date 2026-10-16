#!/bin/sh
# v5: rebuild the research compiler in LLVM-regsw/llvm-build from
# LLVM-regsw/llvm-project (branch v5-linux = xreg fc34b3040 + the unj-auto
# FP-phi crash fix 48b9fa4c7; identical results on all bare-metal v3 runs).
# Never run while an experiment runs (the targets rebuild llvm-symbolizer).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ninja -C "$ROOT/llvm-build" -j"${J:-16}" clang lld llc opt llvm-mc llvm-objdump llvm-ar llvm-ranlib \
    llvm-size llvm-symbolizer llvm-profdata llvm-nm
echo "llvm: $ROOT/llvm-build/bin/clang"
