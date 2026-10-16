# v5: measuring on Linux (Spike + Buildroot)

**Goal (owner, 2026-10-16):** measure the v3 compilers on Linux running on Spike instead of bare metal. Counting covers only the benchmark run (begin/end markers), never the boot.

**Owner's choices:**
- musl, compiled by each compiler under test;
- count everything in the benchmark window (program + kernel), with the kernel share as an extra column;
- v5 = v3 compilers re-measured on Linux;
- new benchmarks: codecs plus minimap2, small inputs, some FP allowed.

## Layout (main checkout, branch `v5-linux`)
- **Repos and builds at the top of LLVM-regsw:**
  - `llvm-project` on branch `v5-linux` → `llvm-build` (`linux/build_llvm.sh`);
  - `spike-xreg` on branch `claude/v5-linux` → `install-xreg` (bare metal) and `install-xreg-ma` (Linux runs, hardware misaligned accesses, D29), both by `linux/build_spike.sh`.
- `linux/` (tracked): scripts, configs, the guest overlay, hostfs, the media recipes, this file.
- `linux/work/` (ignored, rebuildable):
  - `buildroot/`: the Linux build (`linux/build_linux.sh`);
  - `hostfs/`: the plugin and guest tool;
  - `src/`: musl 1.2.5 and the media sources (`linux/media/fetch.sh`);
  - `sysroot/<version>`: musl per compiler version, and `builtins/` (`linux/build_libs.py`);
  - `media-prep/`: the prepared media sources (`linux/media/prepare.py`);
  - `uapi/`: links to the kernel UAPI headers.
- The first build was made in the job worktree `.claude/worktrees/reassessment`, which has its own clones of Spike and the compiler under `linux/work/`. They are now the branches above.

**Rebuild from scratch in the main checkout:**
```sh
sh linux/build_llvm.sh && sh linux/build_spike.sh
rsync -a --exclude /output ~/working_dir/spike-linux/buildroot/ linux/work/buildroot/   # downloads + tree
sh linux/build_linux.sh
python3 linux/build_libs.py builtins && python3 linux/build_libs.py musl r32-v3 r256-v3
sh linux/media/fetch.sh && python3 linux/media/prepare.py
```

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

Build: `sh linux/build_spike.sh` (both Spikes).
