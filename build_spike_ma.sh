#!/bin/sh
# v5: the same Spike (claude/v5-linux) with misaligned loads/stores handled in
# hardware (--enable-misaligned) into linux/work/install-spike-ma. Without it a
# misaligned access traps and Linux emulates it, which (a) costs kernel work and
# (b) breaks prefixed (x32+) loads/stores: the kernel's emulator only decodes
# the 5-bit register field (EXP-042c: ra overwritten, SIGBUS).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$HERE/work"
mkdir -p "$W/spike-xreg/build-ma"
cd "$W/spike-xreg/build-ma"
[ -f Makefile ] || ../configure --prefix="$W/install-spike-ma" --enable-histogram --enable-misaligned > configure.out 2>&1
make -j"${J:-16}" > make.out 2>&1
make install > install.out 2>&1
echo "spike: $W/install-spike-ma/bin/spike"
