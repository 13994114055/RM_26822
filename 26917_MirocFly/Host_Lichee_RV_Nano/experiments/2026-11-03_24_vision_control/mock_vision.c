/*
 * mock_vision.c —— 假视觉源（发送端），用于控制端并行开发
 *
 * 场景：
 *   static  目标静止在中心
 *   sweep   目标正弦左右+上下扫动（默认）
 *   step    目标在 -300 / +300 之间阶跃
 *   loss    每 3 秒丢 1 秒（n=0）
 *   multi   两个目标（-250 / +250），primary=0
 *
 * 用法: ./mock_vision [scenario] [port]
 * 发送: UDP 127.0.0.1:7788，30Hz，vif_frame_t（与 vision_iface.h 一致）
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "vision_iface.h"

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void add_det(vif_frame_t *f, int dx, int dy, int w, int h, int cls, int conf)
{
	int i = f->n;
	if (i >= VIF_MAX_DET) return;
	f->det[i].dx = (int16_t)dx;
	f->det[i].dy = (int16_t)dy;
	f->det[i].w = (int16_t)w;
	f->det[i].h = (int16_t)h;
	/* 框坐标用图像习惯：左上原点、y 下正 */
	f->det[i].x = (int16_t)(f->w / 2 + dx - w / 2);
	f->det[i].y = (int16_t)(f->h / 2 - dy - h / 2);
	f->det[i].area = (uint16_t)(w * h * 0.6);
	f->det[i].cls = (uint8_t)cls;
	f->det[i].conf = (uint8_t)conf;
	f->n++;
}

int main(int argc, char *argv[])
{
	const char *scen = argc > 1 ? argv[1] : "sweep";
	int port = argc > 2 ? atoi(argv[2]) : 7788;

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) { perror("socket"); return 1; }
	struct sockaddr_in dst = {0};
	dst.sin_family = AF_INET;
	dst.sin_port = htons((uint16_t)port);
	dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("mock_vision: scenario=%s -> 127.0.0.1:%d  (30Hz)\n", scen, port);

	uint64_t t0 = now_ns();
	int cnt = 0;
	while (!g_exit) {
		double t = (double)(now_ns() - t0) / 1e9;
		vif_frame_t f;
		memset(&f, 0, sizeof(f));
		f.magic = VIF_MAGIC;
		f.version = VIF_VERSION;
		f.w = 1280;
		f.h = 720;
		f.ts_ns = now_ns();
		f.primary = 0;

		if (strcmp(scen, "static") == 0) {
			add_det(&f, 0, 0, 100, 80, 0, 92);
		} else if (strcmp(scen, "sweep") == 0) {
			int dx = (int)(400.0 * sin(2 * M_PI * 0.20 * t));
			int dy = (int)(120.0 * sin(2 * M_PI * 0.10 * t));
			add_det(&f, dx, dy, 100, 80, 0, 92);
			f.primary = 0;
		} else if (strcmp(scen, "step") == 0) {
			int dx = (fmod(t, 2.0) < 1.0) ? -300 : 300;
			add_det(&f, dx, 0, 100, 80, 0, 92);
		} else if (strcmp(scen, "loss") == 0) {
			if (fmod(t, 3.0) < 2.0) add_det(&f, 100, 0, 100, 80, 0, 92);
			else f.primary = -1;      /* 丢 1 秒 */
		} else if (strcmp(scen, "multi") == 0) {
			add_det(&f, -250, 20, 90, 70, 0, 88);
			add_det(&f, 260, -30, 70, 60, 1, 85);
			f.primary = 0;
		} else {
			fprintf(stderr, "未知场景 %s（static/sweep/step/loss/multi）\n", scen);
			return 1;
		}

		if (f.n == 0) f.primary = -1;
		if (sendto(s, &f, sizeof(f), 0, (struct sockaddr *)&dst, sizeof(dst)) != (ssize_t)sizeof(f)) {
			perror("sendto");
			break;
		}
		if (++cnt % 60 == 0)
			printf("  t=%5.1fs n=%u primary=%d dx=%d\n", t, f.n, f.primary,
			       f.n ? f.det[0].dx : 0);
		usleep(33000);   /* ~30Hz */
	}
	close(s);
	printf("mock_vision: exit\n");
	return 0;
}
