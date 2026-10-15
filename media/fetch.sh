#!/bin/sh
# v5: fetch the media benchmark sources (pinned) into linux/work/src/media,
# then run prepare.py. musl is fetched by hand into linux/work/src (see
# linux/README.md; sha256 a9a118bb...c75e4 for musl-1.2.5.tar.gz).
set -e
D="$(cd "$(dirname "$0")" && pwd)/../work/src/media"
mkdir -p "$D" && cd "$D"
[ -d mibench ] || { git clone -q https://github.com/embecosm/mibench.git && git -C mibench checkout -q 0f3cbcf6b3d589a2b0753cfb9289ddf40b6b9ed8; }
[ -d minimap2 ] || git clone -q --depth 1 --branch v2.28 --recurse-submodules --shallow-submodules https://github.com/lh3/minimap2.git
[ -d zlib ] || git clone -q --depth 1 --branch v1.3.1 https://github.com/madler/zlib.git
git -C minimap2 rev-parse HEAD   # expect 8170693de39b667d11c8931d343c94a23a7690d2
git -C zlib rev-parse HEAD       # expect 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
