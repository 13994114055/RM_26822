# 实验 24：控制端（L1~L4 + 状态机）
#
# i5 本机：make            → vision_bridge / mock_vision（x86，离线调试）
# 板端：  make board       → *.rv（riscv64 musl，静态）
#
# 注意：TC 指向仓库里的官方交叉工具链；若路径不同请改这里。
CC ?= gcc
CFLAGS := -O2 -Wall -Wextra
LDLIBS := -lm
TC := ../../LicheeRV-Nano-Build_official/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-

all: vision_bridge mock_vision

vision_bridge: vision_bridge.c msp.c msp.h vision_iface.h
	$(CC) $(CFLAGS) -o $@ vision_bridge.c msp.c $(LDLIBS)

mock_vision: mock_vision.c vision_iface.h
	$(CC) $(CFLAGS) -o $@ mock_vision.c $(LDLIBS)

board:
	$(TC)gcc $(CFLAGS) -static -o vision_bridge.rv vision_bridge.c msp.c $(LDLIBS)
	$(TC)gcc $(CFLAGS) -static -o mock_vision.rv mock_vision.c $(LDLIBS)

clean:
	rm -f vision_bridge mock_vision *.rv

.PHONY: all board clean
