// hostfs: guest-side client for the spike-hostfs plugin.
//
//   hostfs ls   [hostdir]
//   hostfs get  <hostpath> [localpath]
//   hostfs put  <localpath> [hostpath]
//   hostfs cat  <hostpath>
//   hostfs run  <hostpath> [args...]   fetch the latest copy and exec it
//   hostfs rm   <hostpath>
//   hostfs mkdir <hostdir>
//
// Talks to the device through /dev/mem. Data moves through the shared buffer
// directly (read()/write() straight into the mapping), so there is no extra copy.

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hostfs_regs.h"

static volatile uint64_t *regs;
static char *buf;
static uint64_t buf_size;

#define REG(off) regs[(off) / 8]

static void die(const char *msg, int err)
{
	fprintf(stderr, "hostfs: %s: %s\n", msg, strerror(err));
	exit(1);
}

static void dev_open(void)
{
	const char *env = getenv("HOSTFS_BASE");
	uint64_t base = env ? strtoull(env, NULL, 0) : HOSTFS_DEFAULT_BASE;
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0)
		die("/dev/mem", errno);
	regs = mmap(NULL, HOSTFS_CTRL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
	if (regs == MAP_FAILED)
		die("mmap control page", errno);
	if (REG(HOSTFS_REG_MAGIC) != HOSTFS_MAGIC) {
		fprintf(stderr, "hostfs: no device at 0x%llx (start spike with --device=hostfs,...)\n",
			(unsigned long long)base);
		exit(1);
	}
	buf_size = REG(HOSTFS_REG_BUF_SIZE);
	buf = mmap(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, REG(HOSTFS_REG_BUF_BASE));
	if (buf == MAP_FAILED)
		die("mmap buffer", errno);
	close(fd);
}

static int64_t cmd(const char *path, uint64_t c, uint64_t arg0, uint64_t arg1)
{
	size_t n = strlen(path);
	if (n >= HOSTFS_CTRL_SIZE - HOSTFS_REG_PATH)
		die(path, ENAMETOOLONG);
	memcpy((char *)regs + HOSTFS_REG_PATH, path, n + 1);
	REG(HOSTFS_REG_ARG0) = arg0;
	REG(HOSTFS_REG_ARG1) = arg1;
	__sync_synchronize();
	REG(HOSTFS_REG_CMD) = c;
	__sync_synchronize();
	return (int64_t)REG(HOSTFS_REG_STATUS);
}

static void check(int64_t st, const char *path)
{
	if (st)
		die(path, (int)-st);
}

static uint64_t host_size(const char *path)
{
	check(cmd(path, HOSTFS_CMD_STAT, 0, 0), path);
	return REG(HOSTFS_REG_RESULT);
}

static void get(const char *hpath, const char *lpath, mode_t mode)
{
	uint64_t size = host_size(hpath), off = 0;
	char tmp[4096];
	// write to a temp file and rename, so replacing a running binary works
	snprintf(tmp, sizeof(tmp), "%s.hostfs-tmp", lpath);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0)
		die(tmp, errno);
	while (off < size) {
		uint64_t chunk = size - off < buf_size ? size - off : buf_size;
		check(cmd(hpath, HOSTFS_CMD_READ, off, chunk), hpath);
		uint64_t got = REG(HOSTFS_REG_RESULT);
		if (!got)
			break;
		for (uint64_t w = 0; w < got;) {
			ssize_t r = write(fd, buf + w, got - w);
			if (r < 0)
				die(tmp, errno);
			w += r;
		}
		off += got;
	}
	close(fd);
	if (rename(tmp, lpath))
		die(lpath, errno);
}

static void put(const char *lpath, const char *hpath)
{
	int fd = open(lpath, O_RDONLY);
	if (fd < 0)
		die(lpath, errno);
	uint64_t off = 0;
	for (;;) {
		ssize_t n = 0, r;
		while ((uint64_t)n < buf_size && (r = read(fd, buf + n, buf_size - n)) > 0)
			n += r;
		if (r < 0)
			die(lpath, errno);
		check(cmd(hpath, off ? HOSTFS_CMD_WRITE : HOSTFS_CMD_WRITE_TRUNC, off, n), hpath);
		off += n;
		if ((uint64_t)n < buf_size)
			break;
	}
	close(fd);
}

static int usage(void)
{
	fputs("usage: hostfs ls [dir] | get <host> [local] | put <local> [host] | cat <host>\n"
	      "              | run <host> [args...] | rm <host> | mkdir <hostdir>\n", stderr);
	return 2;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return usage();
	const char *op = argv[1], *a = argc > 2 ? argv[2] : NULL, *b = argc > 3 ? argv[3] : NULL;
	dev_open();

	if (!strcmp(op, "ls")) {
		check(cmd(a ? a : "", HOSTFS_CMD_LIST, 0, 0), a ? a : ".");
		fwrite(buf, 1, REG(HOSTFS_REG_RESULT), stdout);
	} else if (!strcmp(op, "get") && a) {
		char tmp[4096];
		snprintf(tmp, sizeof(tmp), "%s", a);
		get(a, b ? b : basename(tmp), 0755);
	} else if (!strcmp(op, "put") && a) {
		char tmp[4096];
		snprintf(tmp, sizeof(tmp), "%s", a);
		put(a, b ? b : basename(tmp));
	} else if (!strcmp(op, "cat") && a) {
		uint64_t size = host_size(a), off = 0;
		while (off < size) {
			check(cmd(a, HOSTFS_CMD_READ, off, size - off), a);
			uint64_t got = REG(HOSTFS_REG_RESULT);
			if (!got)
				break;
			fwrite(buf, 1, got, stdout);
			off += got;
		}
	} else if (!strcmp(op, "run") && a) {
		char tmp[4096], local[4200];
		snprintf(tmp, sizeof(tmp), "%s", a);
		mkdir("/tmp/hostfs-run", 0755);
		snprintf(local, sizeof(local), "/tmp/hostfs-run/%s", basename(tmp));
		get(a, local, 0755);
		argv[2] = local;
		execv(local, argv + 2);
		die(local, errno);
	} else if (!strcmp(op, "rm") && a) {
		check(cmd(a, HOSTFS_CMD_UNLINK, 0, 0), a);
	} else if (!strcmp(op, "mkdir") && a) {
		check(cmd(a, HOSTFS_CMD_MKDIR, 0, 0), a);
	} else {
		return usage();
	}
	return 0;
}
