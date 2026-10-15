#!/usr/bin/env python3
"""v5: build the Linux user-space libraries the benchmarks link against.

  build_libs.py builtins            compiler-rt builtins, once (plain rv64g, -O2;
                                    shared by every version, like the kernel)
  build_libs.py musl VERSION...     musl 1.2.5 per compiler version (bench/versions.json),
                                    compiled with that version's flags: -riscv-num-gprs,
                                    switches, -march (owner: libc compiled by the
                                    compiler under test). No profile and no register
                                    globals: those are per program. With N = 224 the
                                    global-register range x224..x255 is never allocated.

Outputs: linux/work/builtins/libclang_rt.builtins.a, linux/work/sysroot/<version>/.
Standard library only.
"""
import glob, json, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
W = os.path.join(HERE, "work")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from xreg_bench import CLANG, cfg_flags  # noqa: E402

LLVM_BIN = os.path.dirname(CLANG)
TARGET = ["--target=riscv64-unknown-linux-musl", "-mabi=lp64", "-mno-relax"]
MUSL_SRC = os.path.join(W, "src", "musl-1.2.5")
BUILTINS = os.path.join(W, "builtins", "libclang_rt.builtins.a")
# not needed / not buildable for a static, freestanding RV64 Linux user program
SKIP = {"emutls.c", "enable_execute_stack.c", "gcc_personality_v0.c", "clear_cache.c",
        "cpu_model.c", "os_version_check.c", "trampoline_setup.c", "eprintf.c", "atomic.c"}


def version_cfg(name):
    for v in json.load(open(os.path.join(ROOT, "bench", "versions.json")))["versions"]:
        if v["name"] == name:
            return v["config"]
    sys.exit(f"unknown version {name}")


def builtins():
    src = os.path.join(ROOT, "llvm-project", "compiler-rt", "lib", "builtins")
    out = os.path.join(W, "builtins", "obj")
    os.makedirs(out, exist_ok=True)
    flags = [CLANG] + TARGET + ["-march=rv64g", "-O2", "-fPIC", "-ffreestanding", "-w", "-c"]
    files = [f for f in sorted(glob.glob(os.path.join(src, "*.c"))) if os.path.basename(f) not in SKIP]

    def one(f):
        o = os.path.join(out, os.path.basename(f)[:-2] + ".o")
        r = subprocess.run(flags + [f, "-o", o], capture_output=True, text=True)
        return o if r.returncode == 0 else None

    with ThreadPoolExecutor(16) as ex:
        objs = [o for o in ex.map(one, files) if o]
    if os.path.exists(BUILTINS):
        os.remove(BUILTINS)
    subprocess.check_call([os.path.join(LLVM_BIN, "llvm-ar"), "rcs", BUILTINS] + objs)
    print(f"builtins: {len(objs)} of {len(files)} files -> {BUILTINS}")


def musl(name):
    cfg = dict(version_cfg(name))
    cfg.pop("cflags", None)                 # PolyBench define: not for libc
    flags = [f for f in cfg_flags(dict(cfg, cflags=[]))]
    prefix = os.path.join(W, "sysroot", name)
    build = os.path.join(W, "musl-build", name)
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)
    cc = " ".join([CLANG] + TARGET + flags)
    env = dict(os.environ, CC=cc, AR=os.path.join(LLVM_BIN, "llvm-ar"),
               RANLIB=os.path.join(LLVM_BIN, "llvm-ranlib"), LIBCC=BUILTINS)
    log = open(os.path.join(build, "build.log"), "w")
    subprocess.check_call([os.path.join(MUSL_SRC, "configure"), f"--prefix={prefix}",
                           "--target=riscv64-linux-musl", "--disable-shared"],
                          cwd=build, env=env, stdout=log, stderr=log)
    subprocess.check_call(["make", "-j16"], cwd=build, env=env, stdout=log, stderr=log)
    subprocess.check_call(["make", "install"], cwd=build, env=env, stdout=log, stderr=log)
    print(f"musl {name}: {prefix} (flags: {' '.join(flags)})")


if __name__ == "__main__":
    if sys.argv[1:2] == ["builtins"]:
        builtins()
    elif sys.argv[1:2] == ["musl"]:
        for n in sys.argv[2:]:
            musl(n)
    else:
        sys.exit(__doc__)
