// spike-hostfs register map, shared by the Spike plugin and the guest tool.
#ifndef HOSTFS_REGS_H
#define HOSTFS_REGS_H

#define HOSTFS_MAGIC            0x31305346534f48ULL  /* "HOSFS01" */
#define HOSTFS_VERSION          1

#define HOSTFS_DEFAULT_BASE     0x40000000ULL
#define HOSTFS_DEFAULT_BUF_SIZE (64ULL << 20)
#define HOSTFS_CTRL_SIZE        0x1000

/* Control page (64-bit registers). The data buffer follows at base + CTRL_SIZE. */
#define HOSTFS_REG_MAGIC     0x00  /* ro */
#define HOSTFS_REG_VERSION   0x08  /* ro */
#define HOSTFS_REG_BUF_BASE  0x10  /* ro: physical address of the data buffer */
#define HOSTFS_REG_BUF_SIZE  0x18  /* ro */
#define HOSTFS_REG_CMD       0x20  /* wo: writing runs the command synchronously */
#define HOSTFS_REG_STATUS    0x28  /* ro: 0 or -errno */
#define HOSTFS_REG_ARG0      0x30  /* rw: file offset */
#define HOSTFS_REG_ARG1      0x38  /* rw: length (STAT returns st_mode here) */
#define HOSTFS_REG_RESULT    0x40  /* ro: bytes moved / file size / listing size */
#define HOSTFS_REG_PATH      0x800 /* rw: NUL-terminated path relative to root */

#define HOSTFS_CMD_STAT        1  /* result = size, arg1 = mode */
#define HOSTFS_CMD_READ        2  /* file[arg0, +arg1) -> buf; result = bytes */
#define HOSTFS_CMD_WRITE       3  /* buf[0, arg1) -> file[arg0..] */
#define HOSTFS_CMD_WRITE_TRUNC 4  /* like WRITE, truncating/creating the file first */
#define HOSTFS_CMD_LIST        5  /* "<f|d> <size> <name>\n" lines -> buf; result = bytes */
#define HOSTFS_CMD_MKDIR       6
#define HOSTFS_CMD_UNLINK      7

#endif
