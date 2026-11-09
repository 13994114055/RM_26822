/*
 * vif_play —— 回放录制的 vif_frame_t 数据（152 字节/帧）
 *
 * 用法: ./vif_play <文件> [目标ip:port] [速度倍率] [循环次数]
 *   例: ./vif_play rec.vif 127.0.0.1:7788 1.0 0     （0=无限循环）
 *
 * 按录制时的 ts_ns 间隔回放，倍率>1 加速、<1 减速。
 * 用于：控制端离线调试（不依赖相机和模型）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "vision_iface.h"

#define PKT_SIZE    ((long)sizeof(vif_frame_t))



static uint64_t now_ns(void)
{
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("用法: %s <rec.vif> [ip:port] [倍率] [循环次数(0=无限)]\n", argv[0]);
		return 1;
	}
	const char *path = argv[1];
	const char *spec = argc > 2 ? argv[2] : "127.0.0.1:7788";
	double speed = argc > 3 ? atof(argv[3]) : 1.0;
	int loops = argc > 4 ? atoi(argv[4]) : 1;
	if (speed <= 0) speed = 1.0;

	char ip[64] = "127.0.0.1"; int port = 7788;
	const char *c = strchr(spec, ':');
	if (c) { snprintf(ip, sizeof(ip), "%.*s", (int)(c - spec), spec); port = atoi(c + 1); }
	else snprintf(ip, sizeof(ip), "%s", spec);

	FILE *fp = fopen(path, "rb");
	if (!fp) { perror("fopen"); return 1; }
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	long nframes = size / PKT_SIZE;
	if (nframes <= 0) { fprintf(stderr, "文件里没有完整帧（%ld 字节）\n", size); return 1; }
	printf("回放: %s  %ld 帧  -> %s:%d  倍率 %.2f  循环 %d\n", path, nframes, ip, port, speed, loops);

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET; dst.sin_port = htons((uint16_t)port);
	inet_pton(AF_INET, ip, &dst.sin_addr);

	vif_frame_t *buf = (vif_frame_t *)malloc(size);
	if (fread(buf, PKT_SIZE, nframes, fp) != (size_t)nframes) { fprintf(stderr, "读取失败\n"); return 1; }
	fclose(fp);

	long sent = 0;
	for (int loop = 0; loops == 0 || loop < loops; loop++) {
		uint64_t t0 = now_ns();
		uint64_t base = buf[0].ts_ns;
		for (long i = 0; i < nframes; i++) {
			/* 按录制时间戳节奏发送 */
			uint64_t due = (uint64_t)((double)(buf[i].ts_ns - base) / speed);
			uint64_t elapsed = now_ns() - t0;
			if (due > elapsed) usleep((useconds_t)((due - elapsed) / 1000));
			if (buf[i].magic != VIF_MAGIC) continue;
			sendto(s, &buf[i], PKT_SIZE, 0, (struct sockaddr *)&dst, sizeof(dst));
			sent++;
		}
	}
	printf("回放结束: 共 %ld 帧\n", sent);
	close(s);
	free(buf);
	return 0;
}
