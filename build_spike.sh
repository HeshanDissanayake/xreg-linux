#!/bin/sh
# v5: build the xreg Spike from LLVM-regsw/spike-xreg (branch claude/v5-linux):
#   install-xreg     bare metal (as before; results identical, EXP-040)
#   install-xreg-ma  + hardware misaligned loads/stores, for Linux runs (D29,
#                    INC-001: the kernel's emulation cannot see the xpfx prefix)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
S="$ROOT/spike-xreg"
J=${J:-16}
mkdir -p "$S/build" "$S/build-ma"
cd "$S/build"
[ -f Makefile ] || ../configure --prefix="$ROOT/install-xreg" --enable-histogram > configure.out 2>&1
make -j"$J" > make.out 2>&1 && make install > install.out 2>&1
cd "$S/build-ma"
[ -f Makefile ] || ../configure --prefix="$ROOT/install-xreg-ma" --enable-histogram --enable-misaligned > configure.out 2>&1
make -j"$J" > make.out 2>&1 && make install > install.out 2>&1
echo "spike: $ROOT/install-xreg/bin/spike, $ROOT/install-xreg-ma/bin/spike"
