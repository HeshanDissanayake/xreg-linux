#!/bin/sh
# v5: build the Linux-capable xreg Spike (branch claude/v5-linux) into linux/work/install-spike.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$HERE/work"
mkdir -p "$W/spike-xreg/build"
cd "$W/spike-xreg/build"
[ -f Makefile ] || ../configure --prefix="$W/install-spike" --enable-histogram > configure.out 2>&1
make -j"${J:-16}" > make.out 2>&1
make install > install.out 2>&1
echo "spike: $W/install-spike/bin/spike"
