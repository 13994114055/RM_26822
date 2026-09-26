/*
 * camera.c - 上位机相机抓帧 (基于 sophgo_middleware / libmaix 封装, NV21 修复版)
 *
 * 该封装就是 scpcom 在 scpcom/LicheeSG-Nano-Build 里修好 GC4653 的版本
 * (把 YUYV/UYVY 改成 NV21)。API: mmf_init/mmf_vi_init/mmf_add_vi_channel/mmf_vi_frame_pop
 *
 * 用法: ./camera [width] [height] [frames]
 *   输出 cam_<w>x<h>.raw (NV21)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include "sophgo_middleware.h"
#include "cvi_buffer.h" /* PIXEL_FORMAT_NV21 等 */

static volatile int g_exit = 0;
static void sig_handler(int s) { (void)s; g_exit = 1; }

int main(int argc, char *argv[])
{
	int req_w = 1280, req_h = 720;
	int frames = 1;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	if (argc > 1) req_w = atoi(argv[1]);
	if (argc > 2) req_h = atoi(argv[2]);
	if (argc > 3) frames = atoi(argv[3]);

	printf("=== MirocFly camera (sophgo_middleware, NV21) ===\n");
	printf("request: %dx%d frames=%d\n", req_w, req_h, frames);

	if (mmf_init() != 0) { fprintf(stderr, "mmf_init failed\n"); return -1; }
	printf("sensor id: 0x%x\n", mmf_get_sensor_id());

	if (mmf_vi_init() != 0) { fprintf(stderr, "mmf_vi_init failed\n"); mmf_deinit(); return -1; }

	int ch = mmf_get_vi_unused_channel();
	printf("vi ch = %d\n", ch);
	if (mmf_add_vi_channel(ch, req_w, req_h, PIXEL_FORMAT_NV21) != 0) {
		fprintf(stderr, "mmf_add_vi_channel(%dx%d) failed\n", req_w, req_h);
		mmf_vi_deinit(); mmf_deinit(); return -1;
	}
	printf("vi channel added\n");

	void *data; int len = 0, w = 0, h = 0, fmt = 0;

	/* 预热：前几帧可能没出来 */
	int got = 0;
	for (int i = 0; i < 100 && !g_exit; i++) {
		if (mmf_vi_frame_pop(ch, &data, &len, &w, &h, &fmt) == 0) { got = 1; break; }
		usleep(50 * 1000);
	}
	if (!got) { fprintf(stderr, "frame pop failed (no frame)\n"); goto out; }

	for (int f = 0; f < frames && !g_exit; f++) {
		if (f > 0) {
			if (mmf_vi_frame_pop(ch, &data, &len, &w, &h, &fmt) != 0) break;
		}
		char name[128];
		if (frames == 1)
			snprintf(name, sizeof(name), "cam_%dx%d.raw", w, h);
		else
			snprintf(name, sizeof(name), "cam_%dx%d_%d.raw", w, h, f);
		FILE *fp = fopen(name, "wb");
		if (fp) {
			fwrite(data, 1, len, fp);
			fclose(fp);
			printf("saved %s (%d bytes, %dx%d fmt=%d)\n", name, len, w, h, fmt);
		}
		mmf_vi_frame_free(ch);
	}

out:
	mmf_del_vi_channel(ch);
	mmf_vi_deinit();
	mmf_deinit();
	printf("done\n");
	return 0;
}