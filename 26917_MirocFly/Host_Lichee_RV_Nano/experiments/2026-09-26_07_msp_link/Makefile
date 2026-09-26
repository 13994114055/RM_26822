# MirocFly 实验 07: MSP 链路 (纯 C, 不依赖中间件/板载库)
ROOT := $(abspath $(CURDIR)/../..)
OFF  := $(ROOT)/LicheeRV-Nano-Build_official
TC   := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC   := $(TC)gcc

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
CFLAGS    := $(ARCHFLAGS) -Os -Wall -Wextra

TARGET := msp_test
OBJS   := msp_test.o msp.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) -lm

msp_test.o: msp_test.c msp.h
	$(CC) $(CFLAGS) -c -o $@ $<
msp.o: msp.c msp.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
