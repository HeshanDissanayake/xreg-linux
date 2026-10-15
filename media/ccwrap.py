#!/usr/bin/env python3
"""Recording compiler / archiver wrapper (v5 media benchmarks).

Used as CC=ccwrap.py and AR="ccwrap.py --ar" while a package builds once with
the real tool (CCWRAP_REAL, CCWRAP_REAL_AR). Every call is appended to
CCWRAP_LOG as one JSON line {"cwd", "argv", "ar"}; linux/media/prepare.py
turns the log into per-program recipes (sources, include dirs, defines).
"""
import json, os, sys

ar = sys.argv[1:2] == ["--ar"]
args = sys.argv[2:] if ar else sys.argv[1:]
with open(os.environ["CCWRAP_LOG"], "a") as f:
    f.write(json.dumps({"cwd": os.getcwd(), "argv": args, "ar": ar}) + "\n")
real = os.environ["CCWRAP_REAL_AR" if ar else "CCWRAP_REAL"]
os.execvp(real, [real] + args)
