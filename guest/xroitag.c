/* v5 guest tool: xroitag <id>
 * Executes xroi.tag (custom-1, funct3 = 2, rs1 = id): Spike names the next
 * per-ROI stats file <stats>.<id> and discards any unfinished ROI.
 * Built with the Buildroot (gcc) toolchain; not part of any measurement. */
#include <stdlib.h>

int main(int argc, char **argv)
{
    long id = argc > 1 ? atol(argv[1]) : 0;
    __asm__ volatile(".insn i 0x2b, 2, x0, %0, 0" : : "r"(id));
    return 0;
}
