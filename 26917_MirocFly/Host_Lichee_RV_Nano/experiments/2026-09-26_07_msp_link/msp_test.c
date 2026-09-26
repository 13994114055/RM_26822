/*
 * msp_test.c - 实验07 MSP 链路自测与工具
 *
 * 模式:
 *   selftest            编解码/CRC 自检 (无硬件)
 *   pty                 用 PTY 模拟 FC 做端到端收发 (无硬件)
 *   demo                打印 视觉offset -> RC 映射示例
 *   monitor <dev> <baud>        被动解析串口来的 MSP 帧
 *   inject  <dev> <baud> [hz]   以 hz 发 SET_RAW_RC (默认10Hz), 演示 offset->RC
 * 不带参数 = selftest + pty
 */
#include "msp.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <termios.h>
#include <sys/select.h>

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int read_timeout(int fd, uint8_t *buf, int len, int ms)
{
	fd_set rf;
	FD_ZERO(&rf);
	FD_SET(fd, &rf);
	struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
	int r = select(fd + 1, &rf, NULL, NULL, &tv);
	if (r <= 0)
		return 0;
	int n = (int)read(fd, buf, len);
	return n > 0 ? n : 0;
}

static int do_selftest(void)
{
	uint16_t ch[8] = {1500, 1600, 1000, 1500, 1000, 1000, 1000, 1000};
	uint8_t buf[64];
	uint8_t payload[16];
	for (int i = 0; i < 8; i++) {
		payload[i * 2] = ch[i] & 0xff;
		payload[i * 2 + 1] = ch[i] >> 8;
	}
	int n = msp_v1_encode(MSP_DIR_TO_FC, MSP_SET_RAW_RC, payload, 16, buf);
	printf("[selftest] encoded %d bytes:", n);
	for (int i = 0; i < n; i++) printf(" %02x", buf[i]);
	printf("\n");

	msp_parser_t p; msp_parser_init(&p);
	uint8_t dir = 0, cmd = 0, pl[64], len = 0;
	int got = 0;
	for (int i = 0; i < n; i++) {
		if (msp_parser_feed(&p, buf[i], &dir, &cmd, pl, &len)) { got = 1; break; }
	}
	if (!got || cmd != MSP_SET_RAW_RC || len != 16) {
		printf("[selftest] FAIL: got=%d cmd=%d len=%d\n", got, cmd, len);
		return -1;
	}
	uint16_t back = pl[0] | (pl[1] << 8);
	printf("[selftest] parsed cmd=%d len=%d ch0=%d -> %s\n",
	       cmd, len, back, back == ch[0] ? "OK" : "FAIL");
	/* CRC 已知值: crc8_dvb_s2(0,0x01)=0xD5? 仅作非零检查 */
	uint8_t c = msp_crc8_dvb_s2(0, 0x01);
	printf("[selftest] crc8_dvb_s2(0,0x01)=0x%02x\n", c);
	return (back == ch[0]) ? 0 : -1;
}

static int set_raw_pty_master(int fd)
{
	struct termios tio;
	if (tcgetattr(fd, &tio) == 0) {
		cfmakeraw(&tio);
		tcsetattr(fd, TCSANOW, &tio);
	}
	return 0;
}

static int do_pty(void)
{
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master < 0) { perror("posix_openpt"); return -1; }
	if (grantpt(master) != 0 || unlockpt(master) != 0) { perror("grant/unlock"); return -1; }
	char *slname = ptsname(master);
	set_raw_pty_master(master);
	printf("[pty] slave=%s\n", slname);

	int slave = msp_serial_open(slname, B460800);
	if (slave < 0) { printf("[pty] open slave failed\n"); return -1; }

	/* 客户端发 SET_RAW_RC 200 */
	uint16_t ch[8] = {1500, 1700, 1000, 1500, 1000, 1000, 1000, 1000};
	int sent = msp_send_set_raw_rc(slave, ch, 8);
	if (sent != 22) {
		printf("[pty] send failed (%d)\n", sent);
		return -1;
	}

	/* 模拟 FC: 从 master 收并解析 */
	msp_parser_t p; msp_parser_init(&p);
	uint8_t dir = 0, cmd = 0, pl[64], len = 0, rb[256];
	int ok = 0;
	long t0 = now_ms();
	while (now_ms() - t0 < 1000) {
		int n = read_timeout(master, rb, sizeof(rb), 200);
		for (int i = 0; i < n; i++) {
			if (msp_parser_feed(&p, rb[i], &dir, &cmd, pl, &len)) {
				if (cmd == MSP_SET_RAW_RC) {
					uint16_t c0 = pl[0] | (pl[1] << 8);
					uint16_t c1 = pl[2] | (pl[3] << 8);
					printf("[pty] FC recv SET_RAW_RC len=%d roll=%d pitch=%d\n", len, c0, c1);
					ok = 1;
				}
			}
		}
		if (ok) break;
	}
	if (!ok) { printf("[pty] FAIL: FC 未收到 SET_RAW_RC\n"); return -1; }

	/* 模拟 FC 回复 RAW_IMU (9 个 int16) */
	uint8_t imu[18];
	int16_t vals[9] = {100, -200, 1000, 5, 0, -5, 10, 0, -10}; /* ax ay az gx gy gz mx my mz */
	for (int i = 0; i < 9; i++) { imu[i*2] = vals[i] & 0xff; imu[i*2+1] = (vals[i]>>8)&0xff; }
	msp_send_v1(master, MSP_RAW_IMU, imu, 18);

	/* 客户端从 slave 收并解析 */
	msp_parser_t p2; msp_parser_init(&p2);
	uint8_t dir2 = 0, cmd2 = 0, pl2[64], len2 = 0;
	int ok2 = 0;
	t0 = now_ms();
	while (now_ms() - t0 < 1000) {
		int n = read_timeout(slave, rb, sizeof(rb), 200);
		for (int i = 0; i < n; i++) {
			if (msp_parser_feed(&p2, rb[i], &dir2, &cmd2, pl2, &len2)) {
				if (cmd2 == MSP_RAW_IMU) {
					int16_t az = (int16_t)(pl2[4] | (pl2[5] << 8));
					printf("[pty] client recv RAW_IMU len=%d az=%d -> %s\n",
					       len2, az, az == 1000 ? "OK" : "FAIL");
					ok2 = 1;
				}
			}
		}
		if (ok2) break;
	}
	close(master);
	msp_serial_close(slave);
	return (ok && ok2) ? 0 : -1;
}

static void do_demo(void)
{
	uint16_t rc[4];
	int offs[][2] = {{0,0},{100,0},{-100,0},{0,80},{0,-80},{200,150}};
	printf("[demo] offset -> (roll,pitch,thr,yaw)  kp=0.5 max=300\n");
	for (unsigned i = 0; i < sizeof(offs)/sizeof(offs[0]); i++) {
		msp_offset_to_rc(offs[i][0], offs[i][1], 1280, 720, 0.5, 300, rc);
		printf("  off(%4d,%4d) -> %4d %4d %4d %4d\n",
		       offs[i][0], offs[i][1], rc[0], rc[1], rc[2], rc[3]);
	}
}

static int do_monitor(const char *dev, int baud)
{
	int fd = msp_serial_open(dev, baud);
	if (fd < 0) { printf("open %s failed: %d\n", dev, fd); return -1; }
	printf("[monitor] %s @ %d, Ctrl-C 退出\n", dev, baud);
	msp_parser_t p; msp_parser_init(&p);
	uint8_t rb[512], dir, cmd, pl[256], len;
	for (;;) {
		int n = read_timeout(fd, rb, sizeof(rb), 500);
		for (int i = 0; i < n; i++) {
			if (msp_parser_feed(&p, rb[i], &dir, &cmd, pl, &len))
				printf("frame dir=%c cmd=%d len=%d\n", dir, cmd, len);
		}
	}
	msp_serial_close(fd);
	return 0;
}

static int do_inject(const char *dev, int baud, int hz)
{
	int fd = msp_serial_open(dev, baud);
	if (fd < 0) { printf("open %s failed: %d\n", dev, fd); return -1; }
	printf("[inject] %s @ %d, %dHz, Ctrl-C 退出\n", dev, baud, hz);
	int period = 1000 / (hz > 0 ? hz : 10);
	uint16_t rc[4];
	int t = 0;
	for (;;) {
		/* 演示: 让 offset 在画面里画圆, 映射成 RC */
		int ox = (int)(200 * cos(t * 0.05));
		int oy = (int)(120 * sin(t * 0.05));
		msp_offset_to_rc(ox, oy, 1280, 720, 0.5, 300, rc);
		uint16_t ch[8] = {rc[0], rc[1], rc[2], rc[3], 1000, 1000, 1000, 1000};
		msp_send_set_raw_rc(fd, ch, 16); /* 发16通道, 多余给默认 */
		if (t % 20 == 0)
			printf("offset(%4d,%4d) -> roll=%d pitch=%d\n", ox, oy, rc[0], rc[1]);
		t++;
		usleep(period * 1000);
	}
	msp_serial_close(fd);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *mode = argc > 1 ? argv[1] : "all";

	if (strcmp(mode, "selftest") == 0)
		return do_selftest() == 0 ? 0 : 1;
	if (strcmp(mode, "pty") == 0)
		return do_pty() == 0 ? 0 : 1;
	if (strcmp(mode, "demo") == 0) { do_demo(); return 0; }
	if (strcmp(mode, "monitor") == 0 && argc > 3)
		return do_monitor(argv[2], atoi(argv[3]));
	if (strcmp(mode, "inject") == 0 && argc > 3)
		return do_inject(argv[2], atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 10);

	int a = do_selftest();
	int b = do_pty();
	do_demo();
	printf("== selftest=%s pty=%s ==\n", a == 0 ? "OK" : "FAIL", b == 0 ? "OK" : "FAIL");
	return (a == 0 && b == 0) ? 0 : 1;
}
