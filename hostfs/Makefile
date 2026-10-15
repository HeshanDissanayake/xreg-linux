# SPIKE_INSTALL: Spike install prefix (headers in $(SPIKE_INSTALL)/include)
# CROSS_COMPILE: RISC-V Linux cross-compiler prefix for the guest tool
SPIKE_INSTALL ?= ../install
CROSS_COMPILE ?= riscv64-linux-
OUT ?= build

CXXFLAGS ?= -O2 -g
CFLAGS_GUEST ?= -O2 -static -s

all: $(OUT)/libhostfs.so $(OUT)/hostfs

$(OUT)/libhostfs.so: plugin/hostfs.cc plugin/hostfs_regs.h | $(OUT)
	$(CXX) -std=c++2a $(CXXFLAGS) -fPIC -shared -Iplugin -I$(SPIKE_INSTALL)/include -o $@ $<

$(OUT)/hostfs: guest/hostfs.c plugin/hostfs_regs.h | $(OUT)
	$(CROSS_COMPILE)gcc $(CFLAGS_GUEST) -Iplugin -o $@ $<

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
