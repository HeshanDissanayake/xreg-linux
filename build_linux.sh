#!/bin/sh
# v5: build the Buildroot Linux image for the xreg Spike (linux/work/buildroot),
# the hostfs plugin (against linux/work/install-spike) and the guest tools.
# Based on the owner's ~/working_dir/spike-linux (configs copied, not shared).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$HERE/work"
BR="$W/buildroot"
J=${J:-16}
sed "s|@LINUX@|$HERE|g" "$HERE/br/xreg_defconfig.in" > "$W/xreg_defconfig"
cd "$BR"
[ -f .config ] || make BR2_DEFCONFIG="$W/xreg_defconfig" defconfig > "$W/br-defconfig.log" 2>&1
make toolchain -j"$J" > "$W/br-toolchain.log" 2>&1
CC="$BR/output/host/bin/riscv64-linux-"
# The xreg Spike (2021 base) has the older MMIO plugin API: hostfs_mmio.cc.
S="$W/spike-xreg"
mkdir -p "$W/hostfs"
g++ -std=c++17 -O2 -g -fPIC -shared -I"$S" -I"$S/build" -I"$HERE/hostfs/plugin" \
    -o "$W/hostfs/libhostfs_mmio.so" "$HERE/hostfs/plugin/hostfs_mmio.cc" > "$W/hostfs-build.log" 2>&1
"${CC}gcc" -O2 -static -s -I"$HERE/hostfs/plugin" -o "$W/hostfs/hostfs" "$HERE/hostfs/guest/hostfs.c" >> "$W/hostfs-build.log" 2>&1
mkdir -p "$HERE/br/overlay/usr/bin"
install -m 755 "$W/hostfs/hostfs" "$HERE/br/overlay/usr/bin/hostfs"
install -m 755 "$HERE/br/hrun" "$HERE/br/overlay/usr/bin/hrun"
"${CC}gcc" -O2 -static -s -o "$HERE/br/overlay/usr/bin/xroitag" "$HERE/guest/xroitag.c"
chmod 755 "$HERE/br/overlay/etc/init.d/S99xreg"
make -j"$J" > "$W/br-build.log" 2>&1
# Buildroot links the OpenSBI payload before it re-links the kernel with the
# initramfs, so repack the payload with the final kernel Image.
make opensbi-rebuild > "$W/br-opensbi.log" 2>&1
ls -la "$BR/output/images/fw_payload.elf" "$BR/output/images/fw_payload.bin"
