/*
 * msp_link —— 上位机↔飞控 MSP 链路自检
 *
 * 目的：一次跑完就能判断链路卡在哪一层：
 *   ① 串口能不能打开（线/驱动）
 *   ② 飞控回不回包（波特率/协议/UART 配置）
 *   ③ 回包内容对不对（API 版本、通道、box 映射、授权位）
 *
 * 用法:
 *   ./msp_link [--dev /dev/ttyS0] [--baud 115200] [--n 50] [--send-rc]
 *
 *   --send-rc  每轮同时发一次 SET_RAW_RC(全中位)，用于验证"发"方向
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <termios.h>

#include "msp.h"

static int env_int(const char *k, int d) { const char *v = getenv(k); return v ? atoi(v) : d; }

static int do_request_once(int fd, msp_parser_t *p, uint8_t want, uint8_t *pl, uint8_t *len, int timeout_ms);

static int do_request(int fd, msp_parser_t *p, uint8_t want, uint8_t *pl, uint8_t *len, int timeout_ms)
{
	/* 重试 3 次：每次先清空输入缓冲 + 重置解析器，避免失败命令造成的失步 */
	for (int attempt = 0; attempt < 3; attempt++) {
		tcflush(fd, TCIFLUSH);
		msp_parser_init(p);
		if (do_request_once(fd, p, want, pl, len, timeout_ms) == 0) return 0;
		usleep(30000);
	}
	return -1;
}

static int do_request_once(int fd, msp_parser_t *p, uint8_t want, uint8_t *pl, uint8_t *len, int timeout_ms)
{
	msp_send_request(fd, want);

	/* 循环读到"匹配帧"或超时：一帧可能被拆成多次 read（尤其 40 字节的 MSP_RC） */
	uint64_t deadline = (uint64_t)time(NULL) * 1000 + timeout_ms;
	uint8_t buf[512];
	uint8_t dir, got = 0;
	while ((uint64_t)time(NULL) * 1000 < deadline) {
		struct timeval tv = { 0, 50000 };   /* 50ms 一片 */
		fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
		int sel = select(fd + 1, &rf, NULL, NULL, &tv);
		if (sel <= 0) continue;
		int n = (int)read(fd, buf, sizeof(buf));
		if (n <= 0) continue;
		for (int i = 0; i < n; i++) {
			if (msp_parser_feed(p, buf[i], &dir, &got, pl, len)) {
				if (got == want) return 0;
			}
		}
	}
	return -1;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/ttyS0";
	int baud = 115200, rounds = 20, send_rc = 0;
	int loopback = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--loopback")) loopback = 1;
		else if (!strcmp(argv[i], "--dev") && i + 1 < argc) dev = argv[++i];
		else if (!strcmp(argv[i], "--baud") && i + 1 < argc) baud = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--n") && i + 1 < argc) rounds = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--send-rc")) send_rc = 1;
	}

	printf("=== msp_link ===\n设备: %s  波特率: %d  轮数: %d  发RC: %s\n",
	       dev, baud, rounds, send_rc ? "是" : "否");

	int fd = msp_serial_open(dev, baud);
	if (fd < 0) { fprintf(stderr, "❌ 打不开串口 %s（检查设备名/权限/线）\n", dev); return 2; }
	printf("✅ 串口已打开\n");

	/* 回环测试：TX 与 RX 短接时，发什么都应该收到什么 */
	if (loopback) {
		const char *msg = "PING1234567890";
		write(fd, msg, strlen(msg));
		usleep(50000);
		fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
		struct timeval tv = { 0, 300000 };
		char buf[256]; int n = 0;
		if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) n = (int)read(fd, buf, sizeof(buf));
		printf("发出: %s\n收到: %.*s (%d 字节)\n", msg, n > 0 ? n : 0, buf, n);
		if (n > 0 && memcmp(buf, msg, (size_t)n < strlen(msg) ? (size_t)n : strlen(msg)) == 0)
			printf("✅ 回环通过：板端 UART + 这段线 是好的\n");
		else
			printf("❌ 回环失败：板端 UART 或这段线有问题（检查 TX/RX 是否短接、引脚是否是该 UART）\n");
		msp_serial_close(fd);
		return (n > 0) ? 0 : 1;
	}

	msp_parser_t p; msp_parser_init(&p);
	uint8_t pl[256], len = 0;

	/* --rate：统计 RC / ACTIVEBOXES / BOXIDS 的应答成功率 */
	if (getenv("MSP_RATE")) {
		struct { uint8_t cmd; const char *name; } list[] = {
			{105,"RC"},{113,"ACTIVEBOXES"},{119,"BOXIDS"},{108,"ATTITUDE"}
		};
		printf("\n--- 应答成功率（每命令 20 次）---\n");
		for (unsigned i = 0; i < sizeof(list)/sizeof(list[0]); i++) {
			int ok = 0; uint8_t sample[64]; uint8_t slen = 0;
			for (int k = 0; k < 20; k++) {
				uint8_t tmp[256], tl = 0;
				if (do_request(fd, &p, list[i].cmd, tmp, &tl, 400) == 0) {
					ok++;
					if (!slen) { memcpy(sample, tmp, tl > 64 ? 64 : tl); slen = tl; }
				}
				usleep(20000);
			}
			printf("  cmd %3d %-12s %2d/20  %s", list[i].cmd, list[i].name, ok, ok ? "len=" : "");
			if (ok) { int lim = slen < 32 ? slen : 32; printf("%d [", slen); for (int k = 0; k < lim; k++) printf("%02x ", sample[k]); printf("]"); }
			printf("\n");
		}
		msp_serial_close(fd);
		return 0;
	}

	/* --probe：逐个探测固件支持哪些 MSP 命令 */
	if (getenv("MSP_PROBE")) {
		struct { uint8_t cmd; const char *name; } probe[] = {
			{1,"API_VERSION"},{2,"FC_VARIANT"},{3,"FC_VERSION"},{4,"BOARD_INFO"},
			{101,"STATUS"},{102,"RAW_IMU"},{105,"RC"},{108,"ATTITUDE"},{109,"ALTITUDE"},
			{110,"ANALOG"},{113,"ACTIVEBOXES"},{116,"MOTOR"},{118,"BOXNAMES"},
			{119,"BOXIDS"},{121,"BOX"},{124,"MODES"},{125,"MODE_RANGES"},{150,"SENSOR_STATUS"}
		};
		printf("\n--- MSP 命令探测 ---\n");
		for (unsigned i = 0; i < sizeof(probe)/sizeof(probe[0]); i++) {
			int r = do_request(fd, &p, probe[i].cmd, pl, &len, 400);
			printf("  cmd %3d %-14s %s", probe[i].cmd, probe[i].name, r == 0 ? "✅" : "❌");
			if (r == 0 && len) { printf("  len=%d [", len); for (int k = 0; k < (len < 8 ? len : 8); k++) printf("%02x ", pl[k]); printf("]"); }
			printf("\n");
		}
		msp_serial_close(fd);
		return 0;
	}

	int ok_api = 0, ok_rc = 0, ok_ab = 0, ok_ids = 0;

	/* 1) API 版本 */
	int api_major = -1, api_minor = -1;
	for (int r = 0; r < rounds; r++) {
		if (do_request(fd, &p, MSP_API_VERSION, pl, &len, 300) == 0 && len >= 3) {
			api_major = pl[1]; api_minor = pl[2]; ok_api++;
		} else if (do_request(fd, &p, MSP_STATUS, pl, &len, 300) == 0) {
			break;   /* 老版本没有 API_VERSION，但能回 STATUS 也算通 */
		}
		if (ok_api) break;
	}
	if (ok_api) printf("✅ 飞控应答: MSP API %d.%d\n", api_major, api_minor);
	else printf("⚠️  没拿到 MSP_API_VERSION（可能是老固件，继续试其它命令）\n");

	/* 2) 通道 / box */
	int ch[16]; memset(ch, 0, sizeof(ch));
	uint32_t ab = 0; uint8_t ids[64]; int nids = 0;

	for (int r = 0; r < rounds; r++) {
		if (send_rc) {
			uint16_t rc[16];
			for (int i = 0; i < 16; i++) rc[i] = 1500;
			msp_send_set_raw_rc(fd, rc, 16);
		}
		if (do_request(fd, &p, MSP_RC, pl, &len, 300) == 0 && len >= 8) {
			int k = len / 2; if (k > 16) k = 16;
			for (int i = 0; i < k; i++) ch[i] = pl[2*i] | (pl[2*i+1] << 8);
			ok_rc++;
		}
		if (do_request(fd, &p, MSP_ACTIVEBOXES, pl, &len, 300) == 0 && len >= 4) {
			ab = (uint32_t)pl[0] | ((uint32_t)pl[1] << 8) | ((uint32_t)pl[2] << 16) | ((uint32_t)pl[3] << 24);
			ok_ab++;
		}
		if (do_request(fd, &p, MSP_BOXIDS, pl, &len, 300) == 0 && len >= 1) {
			nids = len > 64 ? 64 : len;
			memcpy(ids, pl, (size_t)nids);
			ok_ids++;
		}
		usleep(50000);
		if (ok_rc && ok_ab && ok_ids) break;
	}

	printf("\n--- 统计（%d 轮）---\n", rounds);
	printf("RC 应答: %d 次\n", ok_rc);
	printf("ACTIVEBOXES 应答: %d 次\n", ok_ab);
	printf("BOXIDS 应答: %d 次\n", ok_ids);

	if (ok_rc) {
		printf("\n通道值（前 8）:\n");
		for (int i = 0; i < 8; i++) printf("  ch%d = %4d\n", i + 1, ch[i]);
	}
	if (ok_ab && ok_ids) {
		printf("\n模式位（按 BOXIDS 顺序，仅列出置位）:\n");
		for (int i = 0; i < nids && i < 32; i++) {
			if (ab & (1u << i)) printf("  bit%-2d pid=%3d (armed=%s)\n", i, ids[i], (i == 0) ? "是" : "否");
		}
		for (int i = 0; i < nids; i++) if (ids[i] == 50) printf("  ★ MSP RC OVERRIDE 在该 bit=%d, 当前=%d\n", i, (int)((ab >> i) & 1));
	}

	printf("\n--- 判定 ---\n");
	if (!ok_rc && !ok_ab) printf("❌ 飞控完全不应答：查线序(TX↔RX)、GND、波特率、UART 是否配成 MSP\n");
	else if (ok_rc && !ok_ab) printf("⚠️ 能收通道但收不到模式位：固件较老或 MSP 版本差异\n");
	else printf("✅ 链路通：串口→MSP→通道/模式位都正常\n");

	msp_serial_close(fd);
	return (ok_rc || ok_ab) ? 0 : 1;
}
