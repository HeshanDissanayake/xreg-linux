#!/bin/sh
# v5: boot the Buildroot Linux on the xreg Spike with a host directory shared
# through hostfs. If the share has jobs.txt, the guest runs it and powers off
# (see br/overlay/etc/init.d/S99xreg); otherwise it boots to a login prompt.
#   SHARE=<dir> STATS=<path-prefix> linux/run_spike.sh [extra spike args]
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$HERE/work"
: "${SPIKE:=$W/install-spike/bin/spike}"
: "${SHARE:=$W/share}"
: "${HOSTFS_BUF:=64M}"
mkdir -p "$SHARE"
export PATH="$W/buildroot/output/host/bin:$PATH"   # Spike runs dtc at start-up
STATSARGS=""
[ -n "$STATS" ] && STATSARGS="--xreg-stats=$STATS --xreg-stats-per-roi --xreg-icache=128:4:64"
# shellcheck disable=SC2086
exec "$SPIKE" $STATSARGS \
    --extlib="$W/hostfs/libhostfs_mmio.so" \
    --device="hostfs,0x40000000,root=$SHARE,size=$HOSTFS_BUF" \
    "$@" \
    "$W/buildroot/output/images/fw_payload.elf"
