/*
 * impact_detect.c - 实验16: 撞击检测 (MSP_RAW_IMU 加速度突增)
 *
 * 思路: 轮询 FC 的 MSP_RAW_IMU(102), 计算加速度模长 |a|;
 *   - 维护慢速基线 baseline(EMA);
 *   - 当 | 瞬时模长 - baseline | 超过阈值(默认 1.8g) -> 判 IMPACT, 打印 + 冷却;
 *   - 适合台架验证: 敲一下/拍一下飞控即触发。
 *
 * 用法: ./impact_detect [serial_dev] [baud] [thr_g] [acc_1g] [rate_hz]
 *   default: /dev/ttyS0 230400 1.8 2048 50
 *
 * 说明: 本板 acc_1G=2048 (见 blackbox 头 acc_1G:2048)。单位为原始 LSB。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h>

#include "msp.h"

static double now_s(void)
{
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ttyS0";
	int baud = argc > 2 ? atoi(argv[2]) : 230400;
	double thr_g = argc > 3 ? atof(argv[3]) : 0.8;   /* 相对基线的突增比(1.0=翻倍) */
	double acc1g = argc > 4 ? atof(argv[4]) : 0;      /* 0=自动(用静止基线当 1g) */
	double rate = argc > 5 ? atof(argv[5]) : 50.0;

	setvbuf(stdout, NULL, _IONBF, 0);

	int fd = msp_serial_open(dev, baud);
	if (fd < 0) { fprintf(stderr, "open %s @ %d failed\n", dev, baud); return 1; }
	printf("=== MirocFly exp16 impact_detect ===\n");
	printf("serial %s @ %d  thr=%.2fg acc1g=%.0f rate=%.0fHz\n", dev, baud, thr_g, acc1g, rate);

	msp_parser_t p; msp_parser_init(&p);

	double baseline = acc1g;      /* 初始假设 1g */
	int have = 0;
	double t_last_impact = -10.0;
	double cooldown = 1.0;        /* 撞击后冷却 1s */
	double period = 1.0 / (rate > 1 ? rate : 1);
	double t0 = now_s(), t_next = t0 + period, t_print = t0 + 1.0;
	double mag_max = 0;

	for (;;) {
		msp_send_request(fd, MSP_RAW_IMU);
		double t = now_s();
		fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
		struct timeval tv = {0, 8000};
		if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
			uint8_t b[512], dir, cmd, pl[256], len;
			int n = (int)read(fd, b, sizeof(b));
			for (int i = 0; i < n; i++) {
				if (!msp_parser_feed(&p, b[i], &dir, &cmd, pl, &len)) continue;
				if (cmd != MSP_RAW_IMU || len < 6) continue;
				int ax = (int16_t)(pl[0] | (pl[1] << 8));
				int ay = (int16_t)(pl[2] | (pl[3] << 8));
				int az = (int16_t)(pl[4] | (pl[5] << 8));
				double mag = sqrt((double)ax * ax + (double)ay * ay + (double)az * az);
				if (mag > mag_max) mag_max = mag;

				if (!have) { baseline = mag; have = 1; }
				double ref = (acc1g > 0) ? acc1g : baseline;   /* 1g 参考(默认取静止基线) */
				double g = mag / ref;
				double dev = mag - baseline;                   /* 有符号偏差 */
				double adev = fabs(dev);
				/* 撞击判定: 相对基线的突增 */
				if (adev > thr_g * ref && (t - t_last_impact) > cooldown) {
					printf("*** IMPACT ***  t=%.2fs  |a|=%.2fg  dev=%.2fg  (ax=%d,ay=%d,az=%d)\n",
					       t - t0, g, dev / ref, ax, ay, az);
					t_last_impact = t;
					baseline = mag;                   /* 立即重锁基线, 防误连发 */
				} else {
					/* 慢速基线(排除突增): 仅在偏差不大时更新 */
					if (adev < 0.3 * ref) baseline += 0.02 * (mag - baseline);
				}

				if (t >= t_print) {
					printf("  |a|=%.2fg  baseline=%.2fg  max=%.2fg\n", g, baseline / ref, mag_max / ref);
					t_print = t + 1.0;
				}
			}
		}
		/* 限速 */
		while (now_s() < t_next) usleep(1000);
		t_next += period;
	}

	msp_serial_close(fd);
	return 0;
}
