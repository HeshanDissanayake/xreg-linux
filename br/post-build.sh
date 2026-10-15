#!/bin/sh
# v5: keep the guest quiet while benchmarks run. No daemons that could wake up
# inside a measured region (syslogd, klogd, crond), no network (no NET in the
# kernel), no sysctl step. $1 = target root directory (Buildroot convention).
set -e
for s in S01syslogd S02klogd S02sysctl S40network S50crond; do
    rm -f "$1/etc/init.d/$s"
done
