# MirocFly 上位机相机模块 Makefile
# 相机层: sophgo_middleware.c (scpcom 修复版, NV21) —— 替代坏的 libmaix_mmf.a
# 交叉编译 (riscv64-linux-musl, SG2002 C906)

ROOT   := $(abspath $(CURDIR)/../..)
NANO   := $(ROOT)/LicheeRV-Nano-Build
TC     := $(NANO)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC     := $(TC)gcc
CXX    := $(TC)g++
MW     := $(NANO)/middleware/v2
COMMON := $(MW)/sample/common
KINC   := $(NANO)/linux_5.10/build/sg2002_licheervnano_sd/riscv/usr/include

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
INCS      := -I. -I$(MW)/include -I$(MW)/include/isp/cv181x -I$(COMMON) \
             -I$(KINC) -I$(MW)/include/linux
CFLAGS    := $(ARCHFLAGS) -Os -Wall $(INCS)

TARGET    := camera
OBJS      := camera.o sophgo_middleware.o
COMM_OBJS := $(wildcard $(COMMON)/*.o)

MW_LIBS := -L$(MW)/lib -lsys -lvenc -lvpu -lvdec -lmisc -lcvi_bin -lcvi_bin_isp \
	-lisp -lisp_algo -lae -laf -lawb -lsns_full -lcvi_ive \
	-L$(MW)/lib/3rd -lini

TAIL_LIBS := -latomic -lpthread -lm

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(COMM_OBJS)
	$(CXX) -o $@ $(OBJS) -Wl,--start-group $(COMM_OBJS) $(MW_LIBS) -Wl,--end-group $(TAIL_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
