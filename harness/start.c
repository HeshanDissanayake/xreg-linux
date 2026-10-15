/* v5 Linux harness (compiled by the compiler under test, linked into every
 * benchmark).
 * - stdout is fully buffered in 1 MiB and flushed at exit, so a benchmark's
 *   printing doesn't add write() system calls inside its measured region (the
 *   Linux counterpart of the bare-metal 64 KiB buffer, D16).
 * - ROI_WHOLE_MAIN: suites without their own ROI hooks are measured over the
 *   whole main (bare metal: crt.S); here main is wrapped with -Wl,--wrap=main. */
#include <stdio.h>
#include "xroi.h"

static char xreg_stdout_buf[1 << 20];

__attribute__((constructor)) static void xreg_stdio_init(void)
{
    setvbuf(stdout, xreg_stdout_buf, _IOFBF, sizeof xreg_stdout_buf);
}

#ifdef ROI_WHOLE_MAIN
#include <stdlib.h>

int __real_main(int argc, char **argv, char **envp);

/* The region ends at exit, whether main returns or calls exit() itself
 * (gsm, adpcm). atexit handlers run before musl flushes stdio, so the final
 * flush stays outside the region, as on bare metal. */
static void xreg_roi_exit(void) { xroi_end(); }

int __wrap_main(int argc, char **argv, char **envp)
{
    atexit(xreg_roi_exit);
    xroi_begin();
    return __real_main(argc, argv, envp);
}
#endif
