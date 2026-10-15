# v5: measuring on Linux (Spike + Buildroot)

**Goal (owner, 2026-10-16):** measure the v3 compilers on Linux running on Spike instead of bare metal. Counting covers only the benchmark run (begin/end markers), never the boot.

**Owner's choices:**
- musl, compiled by each compiler under test;
- count everything in the benchmark window (program + kernel), with the kernel share as an extra column;
- v5 = v3 compilers re-measured on Linux;
- new benchmarks: codecs plus minimap2, small inputs, some FP allowed.

## Layout
- `linux/` (tracked): scripts, configs, this file.
- `linux/work/` (ignored, rebuildable):
  - `spike-xreg/`: clone of `spike-xreg`, branch `claude/v5-linux`. Fetch it into the real repo with `git fetch <path> claude/v5-linux`.
  - `install-spike/`: the v5 Spike.
  - `buildroot/`: the Linux build.
  - `musl-*`: one musl per compiler config.

## Spike changes (`claude/v5-linux`)
1. **Trap-safe prefix.** A trap that hits a prefixed instruction returns to the `xpfx`, so the pair re-executes as a unit. Before, a page fault or timer interrupt right after `xpfx` re-ran the instruction without its prefix, i.e. with the wrong registers.
2. **`--xreg-stats-per-roi`.** Each `xroi.end` writes `<stats>.<n>` and resets the counters and the I-cache model, so one boot measures many runs.
3. **New counters:**
   - `roi_insns_kernel`: instructions in a mode other than the program's (kernel, OpenSBI);
   - `roi_mem` / `roi_mem_kernel`: loads/stores decoded by Spike (incl. RVC). The kernel has no compiler tags, so the total needs these.

   The PC histogram (used for the tag counts and profiles) keeps the program's own PCs only.

**Bare-metal check:**
- `tests/xreg` all pass;
- 24 v3 runs (12 benchmarks × r32/r256) identical to EXP-039;
- `roi_mem` = tag count on all 24.

Build: `sh linux/build_spike.sh`.
