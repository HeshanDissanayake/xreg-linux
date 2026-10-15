# spike-hostfs

Share a host directory with Linux running on Spike: boot once, then pull new
binaries from the host and push results back without rebooting.

It is a self-contained Spike MMIO plugin (`--extlib`/`--device`). **No Spike
source changes are needed.**

```
spike-hostfs/
  plugin/hostfs.cc      Spike plugin  -> build/libhostfs.so
  plugin/hostfs_regs.h  register map (shared by plugin and guest tool)
  guest/hostfs.c        guest CLI     -> build/hostfs (static RISC-V binary)
```

## Use

```sh
spike --extlib=libhostfs.so --device=hostfs,root=/path/to/share[,base=0x40000000][,size=64M] \
      fw_payload.elf
```

In the guest (paths are relative to the shared root):

| command | effect |
|---|---|
| `hostfs ls [dir]` | list a host directory |
| `hostfs run <file> [args]` / `hrun <file>` | fetch the current host copy and exec it |
| `hostfs get <file> [local]` | host → guest |
| `hostfs put <local> [file]` | guest → host |
| `hostfs cat <file>`, `hostfs rm <file>`, `hostfs mkdir <dir>` | |

Hot-reload loop: rebuild on the host into the share, then `hrun prog` in the
guest. Every run fetches the latest file.

## How it works

The device adds two bus regions:

* `base` – 4 KiB control page (MMIO registers + path string).
* `base + 0x1000` – shared data buffer (`size`, default 64 MiB). It derives
  from `abstract_mem_t`, so Spike treats it as RAM. Guest accesses take the
  TLB fast path, and the plugin moves file data with a single
  `pread`/`pwrite` into the buffer.

Writing the `CMD` register runs the operation synchronously inside the store,
so no interrupts, polling, or kernel driver are needed. The guest tool maps
both regions through `/dev/mem` and does `read()`/`write()` straight into the
buffer. Files larger than the buffer are moved in chunks.

The guest can only reach files under `root`: absolute paths and `..` are
rejected. Symlinks inside the share are followed.

## Guest requirements

Kernel: `CONFIG_DEVMEM=y` and `CONFIG_STRICT_DEVMEM` off (the buffer is not
declared as system RAM, so it is not handed to the page allocator).

## Merging into another Spike

1. The plugin needs Spike headers from a build/install of *that* Spike (it
   uses `abstract_device_t`, `abstract_mem_t`, `REGISTER_DEVICE`,
   `sim_t::add_device`), and the Spike binary must export its symbols (stock
   Spike links with `-Wl,--export-dynamic`).
   Build: `make SPIKE_INSTALL=<that spike's prefix> CROSS_COMPILE=<riscv64-linux-gnu->`.
2. Older Spike versions have a different `device_factory_t` signature (no
   `sargs` argument). If the build fails there, adapt `hostfs_parse_from_fdt` /
   `hostfs_generate_dts` to that signature. The device itself does not change.
3. Loading it as a `.so` is the tested path. Compiling it into Spike (adding
   it to `riscv/riscv.mk.in`) is untested. If you try it, make sure the linker
   keeps the object, because nothing references it except the static
   `REGISTER_DEVICE` initializer.

## Limitations

* One request at a time: the control page is shared, so don't run two
  `hostfs` commands concurrently (e.g. on two harts) without a lock.
* `base`/`size` must not overlap guest RAM or other devices. The default
  0x40000000 is free on the stock Spike memory map (RAM at 0x80000000).
