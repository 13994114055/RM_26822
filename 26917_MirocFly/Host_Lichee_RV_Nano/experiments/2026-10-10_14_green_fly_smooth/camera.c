/*
 * camera.c - MirocFly 相机层实现 (Form A)
 *
 * 封装 scpcom 的 sophgo_middleware (mmf_*), 提供简洁的 C API。
 * 关键: 本模块链接的是板上 scpcom 库, 编译用 scpcom 公有头 (见 Makefile)。
 */
#include "camera.h"
#include "sophgo_middleware.h"
#include "cvi_buffer.h"

#include <stdio.h>
#include <unistd.h>

static int g_ch = -1;
static bool g_open = false;
static uint32_t g_seq = 0;

void mf_camera_cfg_default(mf_camera_cfg_t *cfg)
{
	if (!cfg)
		return;
	cfg->width = 1280;
	cfg->height = 720;
	cfg->fps = 30;
	cfg->hmirror = 0;
	cfg->vflip = 0;
}

int mf_camera_open(const mf_camera_cfg_t *cfg)
{
	if (g_open)
		return 0;
	if (!cfg) {
		fprintf(stderr, "mf_camera_open: null cfg\n");
		return -1;
	}
	if (mmf_init() != 0) {
		fprintf(stderr, "mf_camera_open: mmf_init failed\n");
		return -1;
	}
	if (mmf_vi_init() != 0) {
		fprintf(stderr, "mf_camera_open: mmf_vi_init failed\n");
		mmf_deinit();
		return -1;
	}
	g_ch = mmf_get_vi_unused_channel();
	if (g_ch < 0) {
		fprintf(stderr, "mf_camera_open: no free vi channel\n");
		mmf_vi_deinit();
		mmf_deinit();
		return -1;
	}
	if (cfg->hmirror)
		mmf_set_vi_hmirror(g_ch, true);
	if (cfg->vflip)
		mmf_set_vi_vflip(g_ch, true);
	if (mmf_add_vi_channel(g_ch, cfg->width, cfg->height, PIXEL_FORMAT_NV21) != 0) {
		fprintf(stderr, "mf_camera_open: add vi channel %dx%d failed\n", cfg->width, cfg->height);
		mmf_vi_deinit();
		mmf_deinit();
		g_ch = -1;
		return -1;
	}
	g_open = true;
	g_seq = 0;
	return 0;
}

void mf_camera_close(void)
{
	if (g_ch >= 0)
		mmf_del_vi_channel(g_ch);
	if (g_open) {
		mmf_vi_deinit();
		mmf_deinit();
	}
	g_ch = -1;
	g_open = false;
}

bool mf_camera_is_open(void)
{
	return g_open;
}

int mf_camera_sensor_id(void)
{
	return mmf_get_sensor_id();
}

int mf_camera_get_frame(mf_frame_t *out, int timeout_ms)
{
	if (!g_open || !out)
		return -1;
	void *data = NULL;
	int len = 0, w = 0, h = 0, fmt = 0;
	int waited = 0;
	for (;;) {
		if (mmf_vi_frame_pop(g_ch, &data, &len, &w, &h, &fmt) == 0) {
			out->data = data;
			out->size = len;
			out->width = w;
			out->height = h;
			out->format = fmt;
			out->seq = g_seq++;
			return 0;
		}
		if (timeout_ms >= 0 && waited >= timeout_ms)
			return -2;
		usleep(10 * 1000);
		if (timeout_ms >= 0)
			waited += 10;
	}
}

void mf_camera_frame_free(void)
{
	if (g_open)
		mmf_vi_frame_free(g_ch);
}

int mf_camera_warmup(int n)
{
	mf_frame_t f;
	int done = 0;
	for (int i = 0; i < n; i++) {
		if (mf_camera_get_frame(&f, 2000) != 0)
			break;
		mf_camera_frame_free();
		done++;
	}
	return done;
}
