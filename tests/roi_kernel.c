/* v5 setup test: kernel work inside a region is counted separately, and the
 * program's own counts stay exact.
 *   roi_kernel <pages> <iters>
 * Region: touch <pages> fresh pages of a new mmap (one store each, one page
 * fault each), then <iters> iterations of a 5-instruction load/store loop
 * (long enough for timer interrupts). Program instructions are
 * 4*pages + 5*iters + (a few compiler moves between the two blocks),
 * loads/stores exactly pages + 2*iters; everything else is the kernel.
 * Built with the Buildroot gcc (not a benchmark). */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

static volatile long cell;

int main(int argc, char **argv)
{
    long pages = argc > 1 ? atol(argv[1]) : 1024;
    long iters = argc > 2 ? atol(argv[2]) : 2000000;
    char *m = mmap(0, pages * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    long *c = (long *)&cell;
    long p = pages, n = iters;
    char *q = m;
    __asm__ volatile(".insn 4, 0x102b" ::: "memory");          /* xroi.begin */
    __asm__ volatile(
        "1: sb   zero, 0(%1)\n"
        "   add  %1, %1, %2\n"
        "   addi %0, %0, -1\n"
        "   bnez %0, 1b\n"
        : "+r"(p), "+r"(q) : "r"(4096L) : "memory");
    __asm__ volatile(
        "2: ld   t0, 0(%1)\n"
        "   addi t0, t0, 1\n"
        "   sd   t0, 0(%1)\n"
        "   addi %0, %0, -1\n"
        "   bnez %0, 2b\n"
        : "+r"(n) : "r"(c) : "t0", "memory");
    __asm__ volatile(".insn 4, 0x2b" ::: "memory");            /* xroi.end */
    printf("roi_kernel done %ld\n", cell);
    return 0;
}
