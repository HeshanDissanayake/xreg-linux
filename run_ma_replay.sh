#!/bin/sh
# EXP-042d/042r/041r: the minimap2 plain-C SIMDe probe and replays of EXP-042
# and EXP-041 on the Spike with hardware misaligned support (build_spike_ma.sh).
cd "$(dirname "$0")/.."
export XREG_CLANG="$PWD/linux/work/llvm-build/bin/clang"
export XREG_LINUX_SPIKE="$PWD/linux/work/install-spike-ma/bin/spike"
python3 scripts/xreg_bench.py run bench/configs/exp-042d.json -j 8 > results/run-exp042d.log 2>&1 &
python3 scripts/xreg_bench.py run bench/configs/exp-042r.json -j 8 > results/run-exp042r.log 2>&1
wait
python3 scripts/xreg_bench.py run bench/configs/exp-041r.json -j 16 > results/run-exp041r.log 2>&1
echo all-done
