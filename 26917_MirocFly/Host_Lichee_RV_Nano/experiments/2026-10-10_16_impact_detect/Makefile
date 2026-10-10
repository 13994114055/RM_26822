# MirocFly 实验 16: 撞击检测 (纯 C, 只用工具链 + msp)
ROOT := $(abspath $(CURDIR)/../..)
OFF  := $(ROOT)/LicheeRV-Nano-Build_official
TC   := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC   := $(TC)gcc

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
CFLAGS    := $(ARCHFLAGS) -Os -Wall -Wextra

TARGET := impact_detect
OBJS   := impact_detect.o msp.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) -lm

impact_detect.o: impact_detect.c msp.h
	$(CC) $(CFLAGS) -c -o $@ $<
msp.o: msp.c msp.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
