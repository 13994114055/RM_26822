/*
 * rtsp_grab.c - 实验 05: VI 取帧 + VENC(H264/H265) + RTSP 实时预览
 *
 * 数据流 (改编自 scpcom sample_vio.c `_test_vi_venc_h26x_rtsp`):
 *   VI 单通道 pop NV21 帧 ──┬─> (预留 AI/检测钩子)
 *                          └─> mmf_venc_push -> mmf_venc_pop -> rtsp_send_memory_data
 *
 * 用法: ./rtsp_grab [width] [height] [type] [fps] [bind_ip]
 *   type: 1=H265(默认; scpcom 未开放 H264 init)
 *   bind_ip: RTSP 绑定 IP, 缺省自动探测(可能选到 wlan0); 走 usb 网用 10.222.2.1
 *           也可用环境变量 RTSP_IP
 * 观看: ffplay -rtsp_transport tcp rtsp://<板IP>:8554/live
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "maix_mmf.h"
#include "cvi_buffer.h"
#include "rtsp_server.h"

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char *argv[])
{
	int img_w = 0, img_h = 0;
	int rtsp_type = 1;
	int img_fps = 30;
	const char *rtsp_ip = getenv("RTSP_IP");
	const int venc_ch = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	if (argc > 1) img_w = atoi(argv[1]);
	if (argc > 2) img_h = atoi(argv[2]);
	if (argc > 3) rtsp_type = atoi(argv[3]);
	if (argc > 4) img_fps = atoi(argv[4]);
	if (argc > 5) rtsp_ip = argv[5];
	if (rtsp_type != 1) {
		printf("note: scpcom 公有头未开放 H264 init, 强制 H265\n");
		rtsp_type = 1;
	}

	printf("=== MirocFly exp05 rtsp_grab ===\n");
	printf("req: %dx%d type=%s fps=%d\n", img_w, img_h, rtsp_type == 2 ? "H264" : "H265", img_fps);

	if (rtsp_server_init((char *)rtsp_ip, 8554) != 0) {
		fprintf(stderr, "rtsp_server_init failed\n");
		return -1;
	}
	if (rtsp_memory_server_start(rtsp_type) != 0) {
		fprintf(stderr, "rtsp_memory_server_start failed\n");
		rtsp_server_deinit();
		return -1;
	}
	printf("rtsp://%s:%d/live\n", rtsp_get_server_ip(), rtsp_get_server_port());

	if (mmf_init() != 0) {
		fprintf(stderr, "mmf_init failed\n");
		rtsp_server_deinit();
		return -1;
	}

	int max_w = 0, max_h = 0;
	mmf_vi_get_max_size(&max_w, &max_h);
	char *sensor = mmf_get_sensor_name();
	printf("sensor %s max %dx%d\n", sensor ? sensor : "?", max_w, max_h);
	if (img_w <= 0 || img_h <= 0) { img_w = max_w; img_h = max_h; }

	(void)rtsp_type;
	mmf_enc_h265_init(venc_ch, img_w, img_h);
	if (mmf_vi_init() != 0) {		fprintf(stderr, "mmf_vi_init failed\n");
		mmf_deinit();
		rtsp_server_deinit();
		return -1;
	}
	int vi_ch = mmf_get_vi_unused_channel();
	if (mmf_add_vi_channel(vi_ch, img_w, img_h, PIXEL_FORMAT_NV21) != 0) {
		fprintf(stderr, "mmf_add_vi_channel failed\n");
		mmf_vi_deinit();
		mmf_deinit();
		rtsp_server_deinit();
		return -1;
	}
	mmf_vi_set_pop_timeout(100);

	/* 丢弃启动黑帧 (见 difficulty_and_method.md B9) */
	void *data; int size, width, height, format;
	for (int i = 0; i < 10 && !g_exit; i++) {
		if (mmf_vi_frame_pop(vi_ch, &data, &size, &width, &height, &format) == 0)
			mmf_vi_frame_free(vi_ch);
		else
			usleep(20 * 1000);
	}
	printf("warmup done, streaming...\n");

	uint64_t last = now_ms(), timestamp = 0;
	uint8_t *acc = NULL;
	int acc_cap = 0;

	while (!g_exit) {
		if (mmf_vi_frame_pop(vi_ch, &data, &size, &width, &height, &format) != 0) {
			usleep(5 * 1000);
			continue;
		}
		/* --- AI/检测钩子: data = NV21 帧, width x height --- */

		if (mmf_venc_push(venc_ch, (uint8_t *)data, img_w, img_h, PIXEL_FORMAT_NV21) == 0) {
			mmf_stream_t stream;
			if (mmf_venc_pop(venc_ch, &stream) == 0) {
				int total = 0;
				for (int i = 0; i < stream.count; i++)
					total += stream.data_size[i];
				if (total > 0) {
					if (stream.count == 1) {
						rtsp_send_memory_data(timestamp, (uint8_t *)stream.data[0], stream.data_size[0]);
					} else {
						if (total > acc_cap) {
							uint8_t *p = realloc(acc, total);
							if (p) { acc = p; acc_cap = total; }
						}
						if (acc) {
							int off = 0;
							for (int i = 0; i < stream.count; i++) {
								memcpy(acc + off, stream.data[i], stream.data_size[i]);
								off += stream.data_size[i];
							}
							rtsp_send_memory_data(timestamp, acc, off);
						}
					}
				}
				mmf_venc_free(venc_ch);
			}
		}
		mmf_vi_frame_free(vi_ch);

		uint64_t t = now_ms();
		timestamp += t - last;
		last = t;
	}

	free(acc);
	mmf_del_venc_channel(venc_ch);
	mmf_del_vi_channel(vi_ch);
	mmf_vi_deinit();
	mmf_deinit();
	rtsp_server_deinit();
	printf("done\n");
	return 0;
}
