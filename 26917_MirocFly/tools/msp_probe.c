/* msp_probe — 验证"上位机(LicheeRV) → 下位机(FC) MSP 链路"。
 * 主动向 FC 的 MSP 口发请求并打印响应; 收到 dir='>' 即链路通。
 *
 * 编译 (i5, 用官方 RISC-V 工具链, 复用实验07 的 msp.c):
 *   TC=<...>/LicheeRV-Nano-Build_official/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
 *   ${TC}gcc -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d \
 *     -Os -I../../Host_Lichee_RV_Nano/experiments/2026-09-26_07_msp_link \
 *     -o msp_probe msp_probe.c ../../Host_Lichee_RV_Nano/experiments/2026-09-26_07_msp_link/msp.c -lm
 *
 * 用法 (板上): ./msp_probe /dev/ttyS0 230400
 *   - LicheeRV 上位机串口 = UART0 = /dev/ttyS0 (引脚 A16/A17), 需先释放 console getty
 *   - FC UART2 MSP 波特率 = 230400
 *   - 回环自测: A16-A17 短接 -> 会读到 dir='<' 的自己发的帧
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include "msp.h"

/* 数字波特率 -> termios 的 Bxxx 常量
 * (注意: 实验07 的 msp_serial_open 直接把数字传给 cfsetispeed, 真串口会失败) */
static speed_t baud_of(int b) {
    switch (b) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B230400;
    }
}

static const char *cmdname(int c) {
    switch (c) {
        case MSP_STATUS:   return "STATUS";
        case MSP_RAW_IMU:  return "RAW_IMU";
        case MSP_ATTITUDE: return "ATTITUDE";
        default:           return "?";
    }
}

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/ttyS0";
    int baud = argc > 2 ? atoi(argv[2]) : 230400;
    int fd = msp_serial_open(dev, (int)baud_of(baud));
    if (fd < 0) { printf("open %s @ %d failed\n", dev, baud); return 1; }
    printf("opened %s @ %d\n", dev, baud);

    msp_parser_t p; msp_parser_init(&p);
    const uint8_t reqs[] = { MSP_STATUS, MSP_ATTITUDE, MSP_RAW_IMU };
    int got = 0;
    for (int round = 0; round < 3; round++) {
        for (unsigned i = 0; i < sizeof(reqs); i++) msp_send_request(fd, reqs[i]);
        for (int t = 0; t < 8; t++) {
            struct pollfd pf = { fd, POLLIN, 0 };
            if (poll(&pf, 1, 100) > 0) {
                uint8_t buf[256];
                int n = read(fd, buf, sizeof(buf));
                for (int k = 0; k < n; k++) {
                    uint8_t dir, cmd, pl[256], len;
                    if (msp_parser_feed(&p, buf[k], &dir, &cmd, pl, &len)) {
                        got++;
                        printf("recv dir=%c cmd=%d(%s) len=%d", dir, cmd, cmdname(cmd), len);
                        if (cmd == MSP_ATTITUDE && len >= 6) {
                            int16_t roll = pl[0] | (pl[1] << 8);
                            int16_t pitch = pl[2] | (pl[3] << 8);
                            int16_t yaw = pl[4] | (pl[5] << 8);
                            printf(" -> roll=%.1f pitch=%.1f yaw=%.1f", roll / 10.0, pitch / 10.0, yaw / 10.0);
                        }
                        printf("\n");
                    }
                }
            }
        }
        printf("-- round %d done (got=%d) --\n", round + 1, got);
    }
    msp_serial_close(fd);
    printf("TOTAL frames received = %d -> %s\n", got, got > 0 ? "LINK OK" : "NO RESPONSE");
    return got > 0 ? 0 : 2;
}
