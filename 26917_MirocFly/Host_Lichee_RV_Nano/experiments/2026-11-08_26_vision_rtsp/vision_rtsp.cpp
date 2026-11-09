/*
 * vision_rtsp.cpp - 检测 + 画框 + RTSP 推流 + UDP 送控制端
 * 复用 exp10 的 mf/tdl 封装（相机/检测/画框/RTSP）。
 * 用法: ./vision_rtsp <model.cvimodel> [w] [h] [udp_ip] [udp_port]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "mf/tdl.h"
#include "vision_iface.h"

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static inline int16_t c16(long v){ return v < -32768 ? -32768 : (v > 32767 ? 32767 : (int16_t)v); }
static inline uint16_t u16c(long v){ return v < 0 ? 0 : (v > 65535 ? 65535 : (uint16_t)v); }

int main(int argc, char *argv[])
{
	if (argc < 2) { printf("usage: %s <model.cvimodel> [w] [h] [udp_ip] [udp_port]\n", argv[0]); return 1; }
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	const char *model = argv[1];
	int w = argc > 2 ? atoi(argv[2]) : 1280;
	int h = argc > 3 ? atoi(argv[3]) : 720;
	const char *ip = argc > 4 ? argv[4] : "127.0.0.1";
	int port = argc > 5 ? atoi(argv[5]) : 7788;

	if (mf_tdl_open(model, MF_TDL_MODEL_YOLOV8_DET_COCO80) != 0) return 1;
	if (mf_tdl_camera_open(w, h) != 0) { printf("camera open failed\n"); return 1; }
	mf_tdl_stream_open(0, w, h);

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET; dst.sin_port = htons((uint16_t)port);
	inet_pton(AF_INET, ip, &dst.sin_addr);
	printf("=== vision_rtsp === model=%s %dx%d  udp=%s:%d\n", model, w, h, ip, port);

	for (int i = 0; i < 10 && !g_exit; i++) { mf_frame_t *f = mf_tdl_camera_read(200); if (f) mf_tdl_camera_release(f); }
	printf("ready\n");

	int seq = 0;
	while (!g_exit) {
		mf_frame_t *f = mf_tdl_camera_read(500);
		if (!f) { usleep(5000); continue; }

		mf_objects_t objs;
		memset(&objs, 0, sizeof(objs));
		int ok = (mf_tdl_detect(f, &objs) == 0);

		vif_frame_t pkt;
		memset(&pkt, 0, sizeof(pkt));
		pkt.magic = VIF_MAGIC; pkt.version = VIF_VERSION;
		pkt.ts_ns = now_ns(); pkt.w = (uint16_t)w; pkt.h = (uint16_t)h;
		pkt.primary = -1;
		int n = ok ? objs.count : 0;
		if (n > VIF_MAX_DET) n = VIF_MAX_DET;
		int cnt = 0; long best = -1;
		for (int k = 0; k < n; k++) {
			long x1 = objs.objs[k].x1, y1 = objs.objs[k].y1;
			long x2 = objs.objs[k].x2, y2 = objs.objs[k].y2;
			long bw = x2 - x1, bh = y2 - y1;
			if (bw <= 0 || bh <= 0) continue;
			long area = bw * bh;
			vif_det_t *d = &pkt.det[cnt];
			d->dx = c16(x1 + bw / 2 - w / 2);
			d->dy = c16(h / 2 - (y1 + bh / 2));
			d->x = c16(x1); d->y = c16(y1); d->w = c16(bw); d->h = c16(bh);
			d->area = u16c(area);
			d->cls = (uint8_t)objs.objs[k].class_id;
			d->conf = (uint8_t)(objs.objs[k].score * 100.0f + 0.5f);
			if (area > best) { best = area; pkt.primary = (int16_t)cnt; }
			cnt++;
		}
		pkt.n = (uint16_t)cnt;
		sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&dst, sizeof(dst));

		if (ok && cnt > 0) mf_tdl_draw(f, &objs);
		mf_tdl_stream_send(f);

		if (cnt > 0) {
			for (int k = 0; k < cnt; k++)
				printf("[det] seq=%d k=%d cls=%d score=%.3f box=(%.1f,%.1f,%.1f,%.1f)\n",
				       seq, k, objs.objs[k].class_id, (double)objs.objs[k].score,
				       (double)objs.objs[k].x1, (double)objs.objs[k].y1,
				       (double)objs.objs[k].x2, (double)objs.objs[k].y2);
		}
		if (seq % 15 == 0) {
			printf("[dbg] objw=%d objh=%d", objs.width, objs.height);
			for (int k = 0; k < cnt; k++)
				printf(" raw%d(%.0f,%.0f,%.0f,%.0f) sc=%.3f",
				       k, (double)objs.objs[k].x1, (double)objs.objs[k].y1,
				       (double)objs.objs[k].x2, (double)objs.objs[k].y2,
				       (double)objs.objs[k].score);
			printf("\n");
			printf("f%d: n=%d", seq, cnt);
			for (int k = 0; k < cnt; k++)
				printf(" [c%d %.2f dx=%d dy=%d]",
				       pkt.det[k].cls, pkt.det[k].conf / 100.0, pkt.det[k].dx, pkt.det[k].dy);
			printf("\n");
		}
		mf_tdl_camera_release(f);
		seq++;
	}
	close(sock);
	mf_tdl_close();
	printf("bye: %d frames\n", seq);
	return 0;
}
