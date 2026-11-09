CC ?= gcc
CFLAGS := -O2 -Wall -Wextra
all: msp_link msp_link.rv
msp_link: msp_link.c msp.c msp.h
	$(CC) $(CFLAGS) -o $@ msp_link.c msp.c
TC := ../../LicheeRV-Nano-Build_official/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
msp_link.rv: msp_link.c msp.c msp.h
	$(TC)gcc $(CFLAGS) -static -o $@ msp_link.c msp.c
clean:
	rm -f msp_link msp_link.rv
.PHONY: all clean
