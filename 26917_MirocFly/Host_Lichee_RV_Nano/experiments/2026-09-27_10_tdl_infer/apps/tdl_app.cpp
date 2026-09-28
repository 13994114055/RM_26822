/*
 * tdl_app.cpp - 实验10 演示: 相机实时检测 + 画框 + RTSP 叠加推流
 *
 * 用法: ./tdl_app <model.cvimodel> [w] [h] [frames] [rtsp_chn]
 *   frames=0 表示一直跑(按 Ctrl-C 退出)。RTSP 端口/URL 由库决定(见 README)。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <unistd.h>

#include "mf/tdl.h"

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("usage: %s <model.cvimodel> [w] [h] [frames] [rtsp_chn]\n", argv[0]);
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	const char *model_path = argv[1];
	int w = argc > 2 ? atoi(argv[2]) : 1280;
	int h = argc > 3 ? atoi(argv[3]) : 720;
	int frames = argc > 4 ? atoi(argv[4]) : 0;
	int rtsp_chn = argc > 5 ? atoi(argv[5]) : 0;

	printf("=== mf_tdl demo ===\n");
	printf("model=%s  cam=%dx%d  frames=%d  rtsp_chn=%d\n", model_path, w, h, frames, rtsp_chn);

	if (mf_tdl_open(model_path, MF_TDL_MODEL_YOLOV8_DET_COCO80) != 0) return 1;
	if (mf_tdl_camera_open(w, h) != 0) { printf("camera open failed\n"); return 1; }
	mf_tdl_stream_open(rtsp_chn, w, h);

	for (int i = 0; i < 10 && !g_exit; i++) {
		mf_frame_t *f = mf_tdl_camera_read(200);
		if (f) mf_tdl_camera_release(f);
		else usleep(20 * 1000);
	}
	printf("camera ready, streaming...\n");

	int seq = 0, total = 0;
	double t0 = now_ms();
	while (!g_exit) {
		mf_frame_t *f = mf_tdl_camera_read(500);
		if (!f) { usleep(5 * 1000); continue; }

		mf_objects_t objs;
		if (mf_tdl_detect(f, &objs) == 0) {
			total += objs.count;
			mf_tdl_draw(f, &objs);          /* 画框+标签 */
			if (seq % 15 == 0) {
				printf("f%d: %d obj", seq, objs.count);
				for (int k = 0; k < objs.count; k++)
					printf(" [c%d %.2f (%.0f,%.0f,%.0f,%.0f)]",
					       objs.objs[k].class_id, objs.objs[k].score,
					       objs.objs[k].x1, objs.objs[k].y1,
					       objs.objs[k].x2, objs.objs[k].y2);
				printf("\n");
			}
		}
		mf_tdl_stream_send(f);              /* 推 RTSP */
		mf_tdl_camera_release(f);

		seq++;
		if (frames > 0 && seq >= frames) break;
	}

	double dt = (now_ms() - t0) / (seq > 0 ? seq : 1);
	printf("done: frames=%d objects_total=%d avg=%.2f ms/frame (%.1f FPS)\n",
	       seq, total, dt, dt > 0 ? 1000.0 / dt : 0);

	mf_tdl_close();
	return 0;
}
