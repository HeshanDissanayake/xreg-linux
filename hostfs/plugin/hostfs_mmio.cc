// spike-hostfs for the older Spike MMIO plugin API (riscv/mmio_plugin.h, Spike
// 2021, the base of the xreg Spike). Same register map and commands as
// hostfs.cc (which targets the newer device-factory API), so the guest tool is
// unchanged.
//
//   spike --extlib=libhostfs_mmio.so --device=hostfs,0x40000000,root=<dir>[,size=64M] ...
//
// One device covers both regions: the old bus hands every access at or above
// the device base to the device, so offsets < HOSTFS_CTRL_SIZE are the control
// page and the rest is the data buffer (base + 0x1000). The buffer goes through
// Spike's MMIO path (slower than RAM, but only file transfers use it, never a
// measured region). Nothing is added to the device tree; the guest tool finds
// the device at its fixed physical address through /dev/mem.

#include <riscv/mmio_plugin.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hostfs_regs.h"

namespace {

reg_t parse_size(const std::string& s) {
  size_t idx = 0;
  reg_t v = std::stoull(s, &idx, 0);
  switch (idx < s.size() ? s[idx] : 0) {
    case 'k': case 'K': v <<= 10; break;
    case 'm': case 'M': v <<= 20; break;
    case 'g': case 'G': v <<= 30; break;
  }
  return v;
}

}  // namespace

class hostfs_mmio_t {
 public:
  // args: "root=<dir>[,size=<bytes>][,base=<addr>]" (base: must equal the
  // --device base; only reported to the guest, default 0x40000000)
  explicit hostfs_mmio_t(const std::string& args) {
    std::string r = ".";
    size_t pos = 0;
    while (pos < args.size()) {
      size_t end = args.find(',', pos);
      if (end == std::string::npos) end = args.size();
      std::string kv = args.substr(pos, end - pos);
      size_t eq = kv.find('=');
      std::string k = kv.substr(0, eq), v = eq == std::string::npos ? "" : kv.substr(eq + 1);
      if (k == "root") r = v;
      else if (k == "size") sz = parse_size(v);
      else if (k == "base") base = parse_size(v);
      else if (!k.empty()) throw std::runtime_error("hostfs: unknown argument '" + kv + "'");
      pos = end + 1;
    }
    sz = (sz + 0xfff) & ~(reg_t)0xfff;
    char real[PATH_MAX];
    if (!realpath(r.c_str(), real))
      throw std::runtime_error("hostfs: root '" + r + "' does not exist");
    root = real;
    data = (char*)mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (data == MAP_FAILED)
      throw std::runtime_error("hostfs: cannot allocate shared buffer");
    fprintf(stderr, "hostfs: sharing %s (ctrl @0x%llx, %llu MiB buffer @0x%llx)\n", real,
            (unsigned long long)base, (unsigned long long)(sz >> 20),
            (unsigned long long)(base + HOSTFS_CTRL_SIZE));
  }
  ~hostfs_mmio_t() { munmap(data, sz); }

  bool load(reg_t addr, size_t len, uint8_t* bytes) {
    if (addr >= HOSTFS_CTRL_SIZE) {
      reg_t a = addr - HOSTFS_CTRL_SIZE;
      if (a + len < a || a + len > sz) return false;
      memcpy(bytes, data + a, len);
      return true;
    }
    if (addr + len > HOSTFS_CTRL_SIZE) return false;
    if (addr >= HOSTFS_REG_PATH) {
      memcpy(bytes, path + (addr - HOSTFS_REG_PATH), len);
      return true;
    }
    uint64_t regs[HOSTFS_REG_PATH / 8] = {};
    regs[HOSTFS_REG_MAGIC / 8] = HOSTFS_MAGIC;
    regs[HOSTFS_REG_VERSION / 8] = HOSTFS_VERSION;
    regs[HOSTFS_REG_BUF_BASE / 8] = base + HOSTFS_CTRL_SIZE;
    regs[HOSTFS_REG_BUF_SIZE / 8] = sz;
    regs[HOSTFS_REG_STATUS / 8] = (uint64_t)status;
    regs[HOSTFS_REG_ARG0 / 8] = arg0;
    regs[HOSTFS_REG_ARG1 / 8] = arg1;
    regs[HOSTFS_REG_RESULT / 8] = result;
    memcpy(bytes, (uint8_t*)regs + addr, len);
    return true;
  }

  bool store(reg_t addr, size_t len, const uint8_t* bytes) {
    if (addr >= HOSTFS_CTRL_SIZE) {
      reg_t a = addr - HOSTFS_CTRL_SIZE;
      if (a + len < a || a + len > sz) return false;
      memcpy(data + a, bytes, len);
      return true;
    }
    if (addr + len > HOSTFS_CTRL_SIZE) return false;
    if (addr >= HOSTFS_REG_PATH) {
      memcpy(path + (addr - HOSTFS_REG_PATH), bytes, len);
      return true;
    }
    uint64_t v = 0;
    memcpy(&v, bytes, std::min(len, sizeof(v)));
    switch (addr) {
      case HOSTFS_REG_ARG0: arg0 = v; break;
      case HOSTFS_REG_ARG1: arg1 = v; break;
      case HOSTFS_REG_CMD:  run(v); break;
      default: break;
    }
    return true;
  }

 private:
  bool resolve(std::string& out) {
    path[sizeof(path) - 1] = 0;
    std::string rel(path);
    if (!rel.empty() && rel[0] == '/') return false;
    size_t pos = 0;
    while (pos <= rel.size()) {
      size_t end = rel.find('/', pos);
      if (end == std::string::npos) end = rel.size();
      if (rel.compare(pos, end - pos, "..") == 0 && end - pos == 2) return false;
      pos = end + 1;
    }
    out = rel.empty() ? root : root + "/" + rel;
    return true;
  }

  void run(uint64_t cmd) {
    status = 0;
    result = 0;
    std::string p;
    if (!resolve(p)) { status = -EACCES; return; }
    switch (cmd) {
      case HOSTFS_CMD_STAT: {
        struct stat st;
        if (stat(p.c_str(), &st) != 0) { status = -errno; break; }
        result = st.st_size;
        arg1 = st.st_mode;
        break;
      }
      case HOSTFS_CMD_READ: {
        uint64_t len = std::min<uint64_t>(arg1, sz);
        int fd = open(p.c_str(), O_RDONLY);
        if (fd < 0) { status = -errno; break; }
        ssize_t n = pread(fd, data, len, arg0);
        if (n < 0) status = -errno; else result = n;
        close(fd);
        break;
      }
      case HOSTFS_CMD_WRITE:
      case HOSTFS_CMD_WRITE_TRUNC: {
        uint64_t len = std::min<uint64_t>(arg1, sz);
        int flags = O_WRONLY | O_CREAT | (cmd == HOSTFS_CMD_WRITE_TRUNC ? O_TRUNC : 0);
        int fd = open(p.c_str(), flags, 0644);
        if (fd < 0) { status = -errno; break; }
        ssize_t n = pwrite(fd, data, len, arg0);
        if (n < 0) status = -errno; else result = n;
        close(fd);
        break;
      }
      case HOSTFS_CMD_LIST: {
        DIR* d = opendir(p.c_str());
        if (!d) { status = -errno; break; }
        std::string out;
        while (struct dirent* e = readdir(d)) {
          if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
          struct stat st;
          std::string full = p + "/" + e->d_name;
          if (stat(full.c_str(), &st) != 0) continue;
          char line[PATH_MAX + 64];
          snprintf(line, sizeof(line), "%c %llu %s\n", S_ISDIR(st.st_mode) ? 'd' : 'f',
                   (unsigned long long)st.st_size, e->d_name);
          out += line;
        }
        closedir(d);
        result = std::min<uint64_t>(out.size(), sz);
        memcpy(data, out.data(), result);
        break;
      }
      case HOSTFS_CMD_MKDIR:
        if (mkdir(p.c_str(), 0755) != 0 && errno != EEXIST) status = -errno;
        break;
      case HOSTFS_CMD_UNLINK:
        if (unlink(p.c_str()) != 0) status = -errno;
        break;
      default:
        status = -ENOSYS;
    }
  }

  std::string root;
  reg_t base = HOSTFS_DEFAULT_BASE;
  reg_t sz = HOSTFS_DEFAULT_BUF_SIZE;
  char* data = nullptr;
  char path[HOSTFS_CTRL_SIZE - HOSTFS_REG_PATH] = {};
  int64_t status = 0;
  uint64_t arg0 = 0, arg1 = 0, result = 0;
};

static mmio_plugin_registration_t<hostfs_mmio_t> hostfs_mmio_registration("hostfs");
