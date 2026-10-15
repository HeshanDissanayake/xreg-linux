#!/usr/bin/env python3
"""v5 media: independent check of the golden outputs.

Builds every media program from its recipe with the Buildroot gcc (-O2,
static glibc), runs it on the same Linux, and checks stdout + out* files
against bench/golden/<name>.sha256, which comes from our compiler at -O0,
N=32 (EXP-042-golden). Agreement means the golden output does not depend on
the compiler. Standard library only.
"""
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "bench"))
import benchmarks, xreg_bench, xreg_linux  # noqa: E402

GCC = os.path.join(ROOT, "linux", "work", "buildroot", "output", "host", "bin", "riscv64-linux-gcc")
OUT = os.path.join(ROOT, "linux", "work", "gccref")


def build(n, b):
    elf = os.path.join(OUT, n)
    cmd = ([GCC, "-O2", "-static", "-std=gnu99", "-w", "-fcommon",
            "-Wno-error=implicit-function-declaration", "-Wno-error=incompatible-pointer-types",
            "-Wno-error=int-conversion"] +
           sum((["-I", i] for i in b["incs"]), []) + [f"-D{d}" for d in b["defs"]] +
           b["srcs"] + ["-lm", "-lpthread", "-o", elf])
    r = subprocess.run(cmd, capture_output=True, text=True)
    return elf if r.returncode == 0 else None, r.stderr[-300:]


def main():
    os.makedirs(OUT, exist_ok=True)
    reg = benchmarks.registry()
    ms = [n for n in reg if reg[n]["set"] == "media"]
    with ThreadPoolExecutor(16) as ex:
        built = dict(zip(ms, ex.map(lambda n: build(n, reg[n]), ms)))
    jobs = [(i, built[n][0], reg[n]["linux_inputs"], reg[n]["linux_args"])
            for i, n in enumerate(ms) if built[n][0]]
    res = xreg_linux.run_jobs(jobs, os.path.join(OUT, "_run"), parallel=13)
    for i, n in enumerate(ms):
        if not built[n][0]:
            print(f"{n:15} gcc build failed: {built[n][1]}")
            continue
        r = res[i]
        row = {"ok": 1 if r["rc"] == 0 else f"rc={r['rc']}"}
        xreg_bench.check_row(row, reg[n], n, r["out"], r.get("files"))
        print(f"{n:15} gcc -O2 vs golden (clang -O0): {'MATCH' if row['ok'] == 1 else row['ok']}")


if __name__ == "__main__":
    main()
