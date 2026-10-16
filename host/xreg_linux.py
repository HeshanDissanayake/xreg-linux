#!/usr/bin/env python3
"""v5: run benchmark binaries on Buildroot Linux booted on the xreg Spike.

How it works (linux/README.md):
  * Each Spike instance boots linux/work/buildroot/output/images/fw_payload.elf
    once, with a host directory (the "share") attached through hostfs.
  * The guest's init script S99xreg reads jobs.txt from the share, runs every
    job (binary + optional input directory + arguments), copies stdout and the
    exit code back to share/out/, and powers off. Spike then exits.
  * Counting covers only each benchmark's own region: its xroi.begin/xroi.end.
    Before each job the guest runs `xroitag <id>`, so Spike writes that run's
    statistics to <stats>.<id> (--xreg-stats-per-roi). Boot, shell and file
    transfers are never inside a region. Inside it, everything counts (the
    program and the kernel: timer interrupts, page faults, system calls).

  xreg_linux.py boot SHARE STATS_PREFIX [TIMEOUT]   one boot over an existing share
Library: run_jobs(jobs, workdir, nshards) -> {id: (rc, out_text, stats_path)}
Standard library only.
"""
import os, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = os.path.join(ROOT, "linux", "work")
# D29 (INC-001): hardware misaligned loads/stores; the kernel's emulation of
# them cannot see the xpfx prefix and corrupts 256-register programs
SPIKE = os.environ.get("XREG_LINUX_SPIKE") or os.path.join(ROOT, "install-xreg-ma", "bin", "spike")
PAYLOAD = os.path.join(W, "buildroot", "output", "images", "fw_payload.elf")
HOSTFS = os.path.join(W, "hostfs", "libhostfs_mmio.so")
ICACHE = "128:4:64"


def boot(share, stats, timeout=7200, log=None):
    """Boot once over `share`; return (spike exit code, seconds, console text)."""
    env = dict(os.environ, PATH=os.path.join(W, "buildroot", "output", "host", "bin") + ":" + os.environ["PATH"])
    st = [] if os.environ.get("XREG_LINUX_NOSTATS") else \
        [f"--xreg-stats={stats}", "--xreg-stats-per-roi", f"--xreg-icache={ICACHE}"]
    cmd = [SPIKE] + st + [f"--extlib={HOSTFS}", f"--device=hostfs,0x40000000,root={share},size=64M",
                          PAYLOAD]
    t0 = time.time()
    log = log or os.path.join(os.path.dirname(stats) or ".", "console.log")
    with open(log, "w") as lf:
        try:
            rc = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=lf, stderr=subprocess.STDOUT,
                                env=env, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    return rc, time.time() - t0, open(log, errors="replace").read()


def run_jobs(jobs, workdir, parallel=12, per_boot=1, timeout=14400):
    """jobs: list of (id, elf, input_dir_or_None, args_str).
    Returns {id: {"rc": int|str, "out": str, "stats": path|None}}.

    per_boot=1 (default, EXP-040): every job gets a fresh boot. Spike is
    deterministic, so each run starts from the identical machine state and the
    kernel part of its counts is reproducible (in one boot, a repeated run's
    kernel work differed by 3%). `parallel` Spike instances run at once."""
    shutil.rmtree(workdir, ignore_errors=True)
    shards = [jobs[i:i + per_boot] for i in range(0, len(jobs), per_boot)]

    def one(k):
        d = os.path.join(workdir, f"shard{k}")
        share = os.path.join(d, "share")
        os.makedirs(os.path.join(share, "bin"))
        os.makedirs(os.path.join(share, "in"))
        lines = []
        for jid, elf, inp, args in shards[k]:
            b = f"bin/{jid}.elf"
            os.link(elf, os.path.join(share, b)) if os.stat(elf).st_dev == os.stat(share).st_dev \
                else shutil.copy(elf, os.path.join(share, b))
            idir = "-"
            if inp:
                idir = f"in/{jid}"
                if isinstance(inp, (list, tuple)):   # list of input files
                    os.makedirs(os.path.join(share, idir))
                    for f in inp:
                        shutil.copy(f, os.path.join(share, idir, os.path.basename(f)))
                else:
                    shutil.copytree(inp, os.path.join(share, idir))
            lines.append(f"{jid} {b} {idir} {args}".rstrip())
        open(os.path.join(share, "jobs.txt"), "w").write("\n".join(lines) + "\n")
        rc, secs, _ = boot(share, os.path.join(d, "stats"), timeout, os.path.join(d, "console.log"))
        res = {}
        for jid, *_ in shards[k]:
            o = os.path.join(share, "out", f"{jid}.out")
            r = os.path.join(share, "out", f"{jid}.rc")
            s = os.path.join(d, f"stats.{jid}")
            if not os.path.exists(r):
                res[jid] = {"rc": f"no-result(spike {rc})", "out": "", "stats": None}
                continue
            res[jid] = {"rc": int(open(r).read().strip() or -1),
                        "out": open(o, errors="replace").read() if os.path.exists(o) else "",
                        "stats": s if os.path.exists(s) else None,
                        "files": os.path.join(share, "out", f"{jid}.files")}
        return res, secs

    out = {}
    with ThreadPoolExecutor(min(parallel, len(shards)) or 1) as ex:
        for res, secs in ex.map(one, range(len(shards))):
            out.update(res)
    return out


if __name__ == "__main__":
    if sys.argv[1:2] == ["boot"]:
        share, stats = os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
        rc, secs, text = boot(share, stats, int(sys.argv[4]) if len(sys.argv) > 4 else 7200)
        print("\n".join(text.splitlines()[-12:]))
        print(f"spike rc={rc}  wall={secs:.0f}s")
    else:
        sys.exit(__doc__)
