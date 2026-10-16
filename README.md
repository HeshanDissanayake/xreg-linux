# xreg-linux: Linux on the 256-register xreg Spike, for measuring compiled programs

A small Buildroot Linux (kernel 6.6, BusyBox, OpenSBI 1.6; about 10 MB) that boots on the
**xreg Spike** (RISC-V Spike with 256 integer registers through the `xpfx` prefix), plus the
tools to run benchmark binaries on it and count **only while each benchmark runs**: never the
boot, the shell or file transfers.

It is the Linux part (v5) of a research project on compilers for large register files
(an LLVM 23.1.3 fork using up to 256 RISC-V integer registers).

## What is in here
| Path | What |
|---|---|
| `br/` | Buildroot defconfig template, kernel config fragment (SBI console, `norandmaps`, slim), post-build script (no daemons), root overlay with the job runner `etc/init.d/S99xreg` |
| `hostfs/` | **hostfs**: share a host directory with the guest through a Spike MMIO device; guest tool `hostfs`. `plugin/hostfs.cc` is for the current Spike device API; `plugin/hostfs_mmio.cc` is the port to the older (2021) plugin API that the xreg Spike uses |
| `guest/xroitag.c` | guest tool: tags the next measured region so Spike writes that run's statistics to `<stats>.<id>` |
| `host/xreg_linux.py` | host controller: boots Spike once per job (deterministic, byte-identical counts across boots), passes binaries and inputs through hostfs, collects outputs and per-run statistics |
| `harness/` | startup code linked into every benchmark (stdout buffering; region = whole `main` for suites without their own markers) |
| `media/` | MiBench media programs + minimap2: fetch (pinned), a recording build that turns each package into a per-program recipe, golden-output cross-check against gcc |
| `build_*.sh`, `build_libs.py`, `run_*.sh` | build the compiler, Spike (normal + misaligned-capable), the Linux image, musl per compiler version |
| `tests/` | region-counting tests (exact counts, kernel work under page faults/timer interrupts) |
| `RESEARCH-NOTES.md` | the research notes for this part (decisions, verification, numbers) |

## How the measurement works
1. Spike boots `fw_payload.elf` with a host directory attached through hostfs.
2. At boot, `S99xreg` reads `jobs.txt` from the share. For each job it copies the binary and its
   inputs, runs `xroitag <id>`, runs the program, and copies stdout, the exit code and any `out*`
   files back. Then it powers off.
3. The program marks its own region (`xroi.begin` / `xroi.end`). Spike counts instructions,
   loads/stores (program and kernel separately), a PC histogram and an I-cache model only inside
   it, and writes `<stats>.<id>` at its end.

## Dependencies (not in this repo)
- **Spike:** `github.com/HeshanDissanayake/riscv-isa-sim-dev`, branch `claude/v5-linux` (tag `xreg-v5`).
  It has the trap-safe prefix, per-run statistics, `xroi.tag` and static-PIE loading.
  Build it with `--enable-histogram --enable-misaligned`: Linux's emulation of misaligned accesses
  cannot see the `xpfx` prefix.
- **Compiler:** `github.com/HeshanDissanayake/llvm-project`, branch `v5-linux` (tag `xreg-v5`).
- **Buildroot 2025.02** (its download cache), musl 1.2.5 (sha256 `a9a118bb…c75e4`).
- The scripts expect to sit as `linux/` inside the research records repository: the benchmark
  driver, the benchmark registry and `scripts/xreg_bench.py`, which is not published. The
  `host/xreg_linux.py` controller and the image work on their own.

## Ready-made image
The release **`v5`** has `fw_payload.elf` (and its SHA-256) for the xreg Spike:
```sh
spike --extlib=libhostfs_mmio.so --device=hostfs,0x40000000,root=<share>,size=64M fw_payload.elf
```
