#!/usr/bin/env python3
"""v5 media benchmarks + minimap2: prepare sources and per-program recipes.

  prepare.py            configure/build every package once with the Buildroot
                        gcc through ccwrap.py, write linux/media/recipes.json

Sources (pinned; fetched into linux/work/src/media by fetch.sh in this dir):
  MiBench (embecosm/mibench 0f3cbcf): jpeg-6a (cjpeg, djpeg), mad-0.14.2b
  (madplay), lame3.70, tiff-v3.5.4 (tiff2bw, tiff2rgba, tiffdither,
  tiffmedian), gsm (toast), adpcm (rawcaudio, rawdaudio);
  minimap2 v2.28 (8170693, SSE2 kernels through SIMDe) + zlib 1.3.1.

A recipe is what bench/benchmarks.py needs: sources, include dirs, defines,
inputs (files copied next to the program), arguments. The driver compiles the
sources with each compiler under test; the gcc build here only discovers them.
Standard library only.
"""
import json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LINUX = os.path.dirname(HERE)
W = os.path.join(LINUX, "work")
SRC = os.path.join(W, "src", "media")
PREP = os.path.join(W, "media-prep")
GCC = os.path.join(W, "buildroot", "output", "host", "bin", "riscv64-linux-gcc")
GAR = os.path.join(W, "buildroot", "output", "host", "bin", "riscv64-linux-ar")
WRAP = os.path.join(HERE, "ccwrap.py")
MB = os.path.join(PREP, "mibench")

# package -> (source dir under PREP, build steps)
PKGS = {
    "jpeg": ("mibench/consumer/jpeg/jpeg-6a",
             [["make", "CC=@CC", "AR=@AR rc", "AR2=true", "cjpeg", "djpeg"]]),
    # mad's 1999 libtool knows no riscv64: configure as a generic 64-bit Linux
    # host and force libmad's portable 64-bit C fixed-point code (no x86 asm)
    "mad": ("mibench/consumer/mad/mad-0.14.2b",
            [["sh", "-c", "CC=@CC AR=@AR ./configure --host=x86_64-unknown-linux-gnu "
                          "--build=x86_64-unknown-linux-gnu --enable-fpm=64bit "
                          "--disable-nls --disable-debugging --disable-shared"],
             # checkout timestamps make the old automake files look stale: don't regenerate
             ["make", "AR=@AR", "CFLAGS=-O2", "ACLOCAL=true", "AUTOMAKE=true", "AUTOCONF=true",
              "AUTOHEADER=true"]]),
    "lame": ("mibench/consumer/lame/lame3.70",
             [["make", "CC=@CC", "AR=@AR", "BRHIST_SWITCH=", "LIBTERMCAP="]]),
    "tiff": ("mibench/consumer/tiff-v3.5.4",
             [["sh", "-c", "CC=@CC ./configure --noninteractive --target=riscv64-linux"],
              # cross-configured, libtiff 3.5.4 guesses big-endian; RISC-V is little-endian
              ["sed", "-i", "-e", "s/^#define HOST_BIGENDIAN.*/#define HOST_BIGENDIAN\t0/",
               "-e", "s/^#define HOST_FILLORDER.*/#define HOST_FILLORDER FILLORDER_LSB2MSB/",
               "libtiff/port.h"],
              # libtiff 3.5.4 makes int32 a long except on Alpha/MIPS64: 8 bytes on
              # RV64; take its own 64-bit branch on any LP64 target (as later libtiff)
              ["sed", "-i", "s/^#if defined(__alpha) || (defined(_MIPS_SZLONG) && _MIPS_SZLONG == 64)$/"
                            "#if defined(__alpha) || defined(__LP64__) || (defined(_MIPS_SZLONG) \\&\\& _MIPS_SZLONG == 64)/",
               "libtiff/tiff.h"],
              # the build runs mkg3states to generate tables: build that one on the host
              ["sh", "-c", "cd libtiff && cc -I. -o mkg3states mkg3states.c && "
                           "./mkg3states -c const tif_fax3sm.c && cc -I. -o mkversion mkversion.c && "
                           "./mkversion -v ../VERSION -a ../dist/tiff.alpha version.h"],
              ["make", "-C", "libtiff", "CC=@CC", "AR=@AR"],
              ["make", "-C", "tools", "CC=@CC", "MACHLIBS=-lm",
               "tiff2bw", "tiff2rgba", "tiffdither", "tiffmedian"]]),
    "gsm": ("mibench/telecomm/gsm", [["make", "CC=@CC", "AR=@AR", "bin/toast"]]),
    "adpcm": ("mibench/telecomm/adpcm/src", [["make", "CC=@CC", "rawcaudio", "rawdaudio"]]),
}

# program -> (package, output file of its link step, inputs, args)
D = lambda *p: os.path.join(MB, *p)
PROGS = {
    "mb-cjpeg":      ("jpeg", "cjpeg", [D("consumer/jpeg/input_small.ppm")],
                      "-dct int -progressive -opt -outfile out.jpeg input_small.ppm"),
    "mb-djpeg":      ("jpeg", "djpeg", [D("consumer/jpeg/input_small.jpg")],
                      "-dct int -ppm -outfile out.ppm input_small.jpg"),
    "mb-madplay":    ("mad", "madplay", [D("consumer/mad/small.mp3")],
                      "--time=4 --output=wave:out.wav small.mp3"),
    "mb-lame":       ("lame", "lame", [D("consumer/lame/small.wav")], "-S small.wav out.mp3"),
    "mb-tiff2bw":    ("tiff", "tiff2bw", [D("consumer/tiff-data/small.tif")], "small.tif out.tif"),
    "mb-tiff2rgba":  ("tiff", "tiff2rgba", [D("consumer/tiff-data/small.tif")], "-c none small.tif out.tif"),
    "mb-tiffdither": ("tiff", "tiffdither", [D("consumer/tiff-data/smallbw.tif")], "-c g4 smallbw.tif out.tif"),
    "mb-tiffmedian": ("tiff", "tiffmedian", [D("consumer/tiff-data/small.tif")], "small.tif out.tif"),
    "mb-gsm-enc":    ("gsm", "toast", [D("telecomm/gsm/data/small.au")], "-fps -c small.au"),
    "mb-gsm-dec":    ("gsm", "toast", [D("telecomm/gsm/data/small.au.run.gsm")], "-d -fps -c small.au.run.gsm"),
    "mb-adpcm-enc":  ("adpcm", "rawcaudio", [D("telecomm/adpcm/data/small.pcm")], "< small.pcm"),
    "mb-adpcm-dec":  ("adpcm", "rawdaudio", [D("telecomm/adpcm/data/small.adpcm")], "< small.adpcm"),
}


def run_pkg(name):
    rel, steps = PKGS[name]
    d = os.path.join(PREP, rel)
    log = os.path.join(PREP, f"{name}.cclog")
    if os.path.exists(log):
        os.remove(log)
    env = dict(os.environ, CCWRAP_LOG=log, CCWRAP_REAL=GCC, CCWRAP_REAL_AR=GAR,
               PATH=os.path.dirname(GCC) + ":" + os.environ["PATH"])
    # configure scripts split CC on spaces: call the wrapper through one-word shims
    bindir = os.path.join(PREP, "bin")
    os.makedirs(bindir, exist_ok=True)
    cc, ar = os.path.join(bindir, "cc-wrap"), os.path.join(bindir, "ar-wrap")
    open(cc, "w").write(f'#!/bin/sh\nexec {sys.executable} {WRAP} "$@"\n')
    open(ar, "w").write(f'#!/bin/sh\nexec {sys.executable} {WRAP} --ar "$@"\n')
    os.chmod(cc, 0o755)
    os.chmod(ar, 0o755)
    if not os.path.exists(os.path.join(bindir, "ar")):
        os.symlink(ar, os.path.join(bindir, "ar"))   # makefiles that call plain `ar` (lame)
    env["PATH"] = bindir + ":" + env["PATH"]
    out = open(os.path.join(PREP, f"{name}.build.log"), "w")
    for st in steps:
        st = [a.replace("@CC", cc).replace("@AR", ar) for a in st]
        r = subprocess.run(st, cwd=d, env=env, stdout=out, stderr=subprocess.STDOUT)
        if r.returncode:
            print(f"{name}: step failed: {' '.join(st)} (see {out.name})")
            return None
    return [json.loads(l) for l in open(log)]


def recipe(entries, target):
    """Sources, include dirs and defines behind the link step producing `target`."""
    compiles, archives, link = {}, {}, None
    for e in entries:
        a, cwd = e["argv"], e["cwd"]
        if e["ar"]:
            lib = next((x for x in a if x.endswith(".a")), None)
            if lib:
                archives.setdefault(os.path.normpath(os.path.join(cwd, lib)), []).extend(
                    os.path.normpath(os.path.join(cwd, x)) for x in a if x.endswith(".o"))
            continue
        srcs = [x for x in a if x.endswith(".c")]
        if "-c" in a and len(srcs) == 1:
            o = a[a.index("-o") + 1] if "-o" in a else os.path.basename(srcs[0])[:-2] + ".o"
            compiles[os.path.normpath(os.path.join(cwd, o))] = (os.path.normpath(os.path.join(cwd, srcs[0])), a, cwd)
        elif "-o" in a and os.path.basename(a[a.index("-o") + 1]) == target:
            link = e
    if not link:
        return None
    a, cwd = link["argv"], link["cwd"]
    objs = []
    for x in a:
        p = os.path.normpath(os.path.join(cwd, x))
        if x.endswith(".o"):
            objs.append(p)
        elif x.endswith(".a") and p in archives:
            objs += archives[p]
        elif x.startswith("-L"):
            pass
        elif x.startswith("-l") and x not in ("-lm", "-lc", "-lpthread"):
            # in-tree library given as -lNAME: find the archive by name
            for lib, members in archives.items():
                if os.path.basename(lib) == f"lib{x[2:]}.a":
                    objs += members
        elif x.endswith(".c"):
            # compiled and linked in one step
            compiles[p + ".o"] = (p, a, cwd)
            objs.append(p + ".o")
    srcs, incs, defs = [], [], []
    for o in dict.fromkeys(objs):
        if o not in compiles:
            # compiled without -o in another directory, then moved (gsm): match by name
            m = [k for k in compiles if os.path.basename(k) == os.path.basename(o)]
            if len(m) != 1:
                return {"error": f"no compile step for {o}"}
            o = m[0]
        s, ca, ccwd = compiles[o]
        srcs.append(s)
        for i, x in enumerate(ca):
            if x.startswith("-I"):
                v = x[2:] or ca[i + 1]
                incs.append(os.path.normpath(os.path.join(ccwd, v)))
            elif x.startswith("-D"):
                defs.append(x[2:] or ca[i + 1])
        incs.append(ccwd)   # "" includes resolve against the compile directory
    return {"srcs": list(dict.fromkeys(srcs)), "incs": list(dict.fromkeys(incs)),
            "defs": list(dict.fromkeys(defs))}


def minimap2_recipe():
    mm = os.path.join(SRC, "minimap2")
    z = os.path.join(SRC, "zlib")
    objs = ("kthread kalloc misc bseq sketch sdust options index lchain align hit seed map format "
            "pe esterr splitidx ksw2_ll_sse ksw2_extz2_sse ksw2_extd2_sse ksw2_exts2_sse main").split()
    zs = ("adler32 crc32 deflate infback inffast inflate inftrees trees zutil compress uncompr "
          "gzclose gzlib gzread gzwrite").split()
    return {"srcs": [os.path.join(mm, f"{o}.c") for o in objs] + [os.path.join(z, f"{s}.c") for s in zs],
            # minimap2's own SIMDe path (USE_SIMDE); __SSE2__ enables its SSE2
            # kernels, SIMDE_NO_NATIVE keeps SIMDe in portable C (no x86 intrinsics)
            "incs": [os.path.join(mm, "lib", "simde"), mm, z],
            "defs": ["HAVE_KALLOC", "USE_SIMDE", "SIMDE_ENABLE_NATIVE_ALIASES", "SIMDE_NO_NATIVE",
                     "__SSE2__", "_FILE_OFFSET_BITS=64"],
            "cflags": ["-fsigned-char"]}


def main():
    if os.path.exists(PREP):
        shutil.rmtree(PREP)
    shutil.copytree(os.path.join(SRC, "mibench"), MB, symlinks=True)
    logs = {p: run_pkg(p) for p in PKGS}
    recipes = {}
    for name, (pkg, target, inputs, args) in PROGS.items():
        r = recipe(logs[pkg], target) if logs[pkg] else None
        if not r or "error" in r:
            print(f"{name}: no recipe ({r and r.get('error')})")
            continue
        recipes[name] = dict(r, inputs=inputs, args=args, suite="media")
    recipes["mm2-mt"] = dict(minimap2_recipe(), suite="minimap2",
                             inputs=[os.path.join(SRC, "minimap2", "test", f) for f in ("MT-human.fa", "MT-orang.fa")],
                             args="-a -t 1 -o out.sam MT-human.fa MT-orang.fa")
    root = os.path.dirname(LINUX)
    for r in recipes.values():   # paths relative to the repo root (bench/benchmarks.py joins them)
        for k in ("srcs", "incs", "inputs"):
            r[k] = [os.path.relpath(x, root) for x in r[k]]
    json.dump(recipes, open(os.path.join(HERE, "recipes.json"), "w"), indent=1)
    for n, r in recipes.items():
        print(f"{n:15} {len(r['srcs']):3} sources  {r['args']}")


if __name__ == "__main__":
    main()
