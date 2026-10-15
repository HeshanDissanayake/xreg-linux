/* v5 setup test: a region with a known instruction count, run under Linux.
 * Built with the Buildroot gcc (not a benchmark). The loop body is fixed
 * assembly: per iteration 1 load, 1 store, 5 instructions, so the region has
 * 5*N (+ the xroi.end) user instructions and N loads + N stores; anything
 * more is the kernel (timer interrupts, page faults) inside the window. */
#include <stdio.h>
#include <stdlib.h>

static volatile long buf[512];

int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 100000;
    long *p = (long *)buf;
    __asm__ volatile(".insn 4, 0x102b" ::: "memory");          /* xroi.begin */
    __asm__ volatile(
        "1: ld   t0, 0(%1)\n"
        "   addi t0, t0, 1\n"
        "   sd   t0, 0(%1)\n"
        "   addi %0, %0, -1\n"
        "   bnez %0, 1b\n"
        : "+r"(n) : "r"(p) : "t0", "memory");
    __asm__ volatile(".insn 4, 0x2b" ::: "memory");            /* xroi.end */
    printf("roi_loop done %ld\n", buf[0]);
    return 0;
}
