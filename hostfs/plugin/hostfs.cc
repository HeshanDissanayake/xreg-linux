// spike-hostfs: share a host directory with a guest running on Spike.
//
// Self-contained MMIO plugin loaded with:
//   spike --extlib=libhostfs.so --device=hostfs,root=<dir>[,base=<addr>][,size=<bytes>] ...
//
// It adds two regions to the bus:
//   base           : 4 KiB control page (registers + path string), MMIO
//   base + 0x1000  : shared data buffer. This one is an abstract_mem_t, so the
//                    guest accesses it at plain RAM speed (it goes through the
//                    MMU's TLB fast path, not the MMIO slow path), and the
//                    plugin fills/drains it with memcpy.
//
// Commands run synchronously inside the CMD register store, so the guest just
// writes CMD and then reads STATUS/RESULT. See hostfs_regs.h for the protocol.

#include <riscv/abstract_device.h>
#include <riscv/devices.h>
#include <riscv/sim.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hostfs_regs.h"

// Shared buffer: contiguous host memory exposed to the guest as RAM.
class hostfs_buf_t : public abstract_mem_t {
 public:
  explicit hostfs_buf_t(reg_t size) : sz(size) {
    data = (char*)mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (data == MAP_FAILED)
      throw std::runtime_error("hostfs: cannot allocate shared buffer");
  }
  ~hostfs_buf_t() override { munmap(data, sz); }

  bool load(reg_t addr, size_t len, uint8_t* bytes) override {
    if (addr + len < addr || addr + len > sz) return false;
    memcpy(bytes, data + addr, len);
    return true;
  }
  bool store(reg_t addr, size_t len, const uint8_t* bytes) override {
    if (addr + len < addr || addr + len > sz) return false;
    memcpy(data + addr, bytes, len);
    return true;
  }
  char* contents(reg_t addr) override { return data + addr; }
  reg_t size() override { return sz; }
  void dump(std::ostream&) override {}

  char* data;
  reg_t sz;
};

class hostfs_t : public abstract_device_t {
 public:
  hostfs_t(const std::string& root, reg_t buf_base, std::shared_ptr<hostfs_buf_t> buf)
    : root(root), buf_base(buf_base), buf(std::move(buf)) {}

  bool load(reg_t addr, size_t len, uint8_t* bytes) override {
    if (addr + len > HOSTFS_CTRL_SIZE) return false;
    if (addr >= HOSTFS_REG_PATH) {
      memcpy(bytes, path + (addr - HOSTFS_REG_PATH), len);
      return true;
    }
    uint64_t regs[HOSTFS_REG_PATH / 8] = {};
    regs[HOSTFS_REG_MAGIC / 8] = HOSTFS_MAGIC;
    regs[HOSTFS_REG_VERSION / 8] = HOSTFS_VERSION;
    regs[HOSTFS_REG_BUF_BASE / 8] = buf_base;
    regs[HOSTFS_REG_BUF_SIZE / 8] = buf->sz;
    regs[HOSTFS_REG_STATUS / 8] = (uint64_t)status;
    regs[HOSTFS_REG_ARG0 / 8] = arg0;
    regs[HOSTFS_REG_ARG1 / 8] = arg1;
    regs[HOSTFS_REG_RESULT / 8] = result;
    memcpy(bytes, (uint8_t*)regs + addr, len);
    return true;
  }

  bool store(reg_t addr, size_t len, const uint8_t* bytes) override {
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
      default: break;  // read-only / reserved
    }
    return true;
  }

  reg_t size() override { return HOSTFS_CTRL_SIZE; }

 private:
  // Resolve the guest-supplied path under root; rejects absolute paths and "..".
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
      case HOSTFS_CMD_READ: {  // file[arg0 .. arg0+arg1) -> buf[0..]
        uint64_t len = std::min<uint64_t>(arg1, buf->sz);
        int fd = open(p.c_str(), O_RDONLY);
        if (fd < 0) { status = -errno; break; }
        ssize_t n = pread(fd, buf->data, len, arg0);
        if (n < 0) status = -errno; else result = n;
        close(fd);
        break;
      }
      case HOSTFS_CMD_WRITE:   // buf[0..arg1) -> file[arg0..]
      case HOSTFS_CMD_WRITE_TRUNC: {
        uint64_t len = std::min<uint64_t>(arg1, buf->sz);
        int flags = O_WRONLY | O_CREAT | (cmd == HOSTFS_CMD_WRITE_TRUNC ? O_TRUNC : 0);
        int fd = open(p.c_str(), flags, 0644);
        if (fd < 0) { status = -errno; break; }
        ssize_t n = pwrite(fd, buf->data, len, arg0);
        if (n < 0) status = -errno; else result = n;
        close(fd);
        break;
      }
      case HOSTFS_CMD_LIST: {  // "<type> <size> <name>\n" lines -> buf
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
        result = std::min<uint64_t>(out.size(), buf->sz);
        memcpy(buf->data, out.data(), result);
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
  reg_t buf_base;
  std::shared_ptr<hostfs_buf_t> buf;
  char path[HOSTFS_CTRL_SIZE - HOSTFS_REG_PATH] = {};
  int64_t status = 0;
  uint64_t arg0 = 0, arg1 = 0, result = 0;
};

namespace {

struct hostfs_args_t {
  std::string root = ".";
  reg_t base = HOSTFS_DEFAULT_BASE;
  reg_t size = HOSTFS_DEFAULT_BUF_SIZE;
};

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

hostfs_args_t parse_args(const std::vector<std::string>& sargs) {
  hostfs_args_t a;
  for (const auto& kv : sargs) {
    size_t eq = kv.find('=');
    std::string k = kv.substr(0, eq), v = eq == std::string::npos ? "" : kv.substr(eq + 1);
    if (k == "root") a.root = v;
    else if (k == "base") a.base = parse_size(v);
    else if (k == "size") a.size = parse_size(v);
    else if (!k.empty()) throw std::runtime_error("hostfs: unknown argument '" + kv + "'");
  }
  a.size = (a.size + 0xfff) & ~(reg_t)0xfff;
  return a;
}

hostfs_t* hostfs_parse_from_fdt(const void*, const sim_t* sim, reg_t* base, const std::vector<std::string>& sargs) {
  hostfs_args_t a = parse_args(sargs);
  char real[PATH_MAX];
  if (!realpath(a.root.c_str(), real))
    throw std::runtime_error("hostfs: root '" + a.root + "' does not exist");

  // The device factory API returns a single device; the data buffer is a second
  // bus region, so register it directly. sim_t::add_device is public, and the
  // const comes only from the factory signature.
  reg_t buf_base = a.base + HOSTFS_CTRL_SIZE;
  auto buf = std::make_shared<hostfs_buf_t>(a.size);
  const_cast<sim_t*>(sim)->add_device(buf_base, buf);

  fprintf(stderr, "hostfs: sharing %s (ctrl @0x%llx, %llu MiB buffer @0x%llx)\n", real,
          (unsigned long long)a.base, (unsigned long long)(a.size >> 20), (unsigned long long)buf_base);
  *base = a.base;
  return new hostfs_t(real, buf_base, buf);
}

std::string hostfs_generate_dts(const sim_t*, const std::vector<std::string>& sargs) {
  hostfs_args_t a = parse_args(sargs);
  reg_t buf_base = a.base + HOSTFS_CTRL_SIZE;
  char s[512];
  snprintf(s, sizeof(s),
           "    hostfs@%llx {\n"
           "      compatible = \"spike,hostfs\";\n"
           "      reg = <0x%x 0x%x 0x0 0x%x>, <0x%x 0x%x 0x%x 0x%x>;\n"
           "    };\n",
           (unsigned long long)a.base,
           (unsigned)(a.base >> 32), (unsigned)a.base, HOSTFS_CTRL_SIZE,
           (unsigned)(buf_base >> 32), (unsigned)buf_base, (unsigned)(a.size >> 32), (unsigned)a.size);
  return s;
}

}  // namespace

REGISTER_DEVICE(hostfs, hostfs_parse_from_fdt, hostfs_generate_dts)
