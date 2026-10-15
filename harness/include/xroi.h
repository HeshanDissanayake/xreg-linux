/* Region-of-interest markers for the xreg Spike (docs/infra/xreg-encoding.md). */
#ifndef XROI_H
#define XROI_H
static inline void xroi_begin(void) { __asm__ volatile(".insn 4, 0x102b" ::: "memory"); }
static inline void xroi_end(void) { __asm__ volatile(".insn 4, 0x2b" ::: "memory"); }
#endif
