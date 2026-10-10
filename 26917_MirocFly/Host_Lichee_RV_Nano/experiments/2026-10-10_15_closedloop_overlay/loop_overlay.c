/*
 * loop_overlay.c - 实验15: 闭环可视化叠加 (C 侧: MMF/VI/VENC/RTSP + MSP 闭环)
 *
 *   每帧: VI pop(NV21) -> om_process()[C++: 检测+平滑+画框] -> 计算 RC
 *         -> MSP SET_RAW_RC + 读 ATTITUDE/RAW_IMU -> VENC(H265) -> RTSP :8554/live
 *
 * 用法: ./loop_overlay [w] [h] [type] [fps] [bind_ip] [serial_dev] [baud]
 *   serial_dev 缺省 /dev/ttyS0; 传 none 则只推流。
 * 观看: ffplay -rtsp_transport tcp rtsp://10.222.2.1:8554/live
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h>

#include "maix_mmf.h"
#include "cvi_buffer.h"
#include "rtsp_server.h"
#include "msp.h"
#include "overlay_api.h"

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
	int img_w = 0, img_h = 0, rtsp_type = 1, img_fps = 30;
	const char *rtsp_ip = getenv("RTSP_IP");
	const char *serial_dev = "/dev/ttyS0";
	int baud = 230400;
	const int venc_ch = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	if (argc > 1) img_w = atoi(argv[1]);
	if (argc > 2) img_h = atoi(argv[2]);
	if (argc > 3) rtsp_type = atoi(argv[3]);
	if (argc > 4) img_fps = atoi(argv[4]);
	if (argc > 5) rtsp_ip = argv[5];
	if (argc > 6) serial_dev = argv[6];
	if (argc > 7) baud = atoi(argv[7]);
	if (rtsp_type != 1) rtsp_type = 1;

	printf("=== MirocFly exp15 loop_overlay ===\n");
	printf("req %dx%d fps=%d rtsp_ip=%s msp=%s@%d\n", img_w, img_h, img_fps,
	       rtsp_ip ? rtsp_ip : "(auto)", serial_dev, baud);

	/* ---- MSP ---- */
	int mfd = -1, use_msp = (serial_dev && strcmp(serial_dev, "none") != 0);
	if (use_msp) {
		mfd = msp_serial_open(serial_dev, baud);
		if (mfd < 0) { fprintf(stderr, "serial open %s failed\n", serial_dev); use_msp = 0; }
		else printf("serial %s @ %d opened\n", serial_dev, baud);
	}

	/* ---- RTSP ---- */
	if (rtsp_server_init((char *)rtsp_ip, 8554) != 0) { fprintf(stderr, "rtsp_server_init failed\n"); return -1; }
	if (rtsp_memory_server_start(rtsp_type) != 0) { fprintf(stderr, "rtsp start failed\n"); rtsp_server_deinit(); return -1; }
	printf("rtsp://%s:%d/live\n", rtsp_get_server_ip(), rtsp_get_server_port());

	/* ---- MMF/VI/VENC ---- */
	if (mmf_init() != 0) { rtsp_server_deinit(); return -1; }
	int max_w = 0, max_h = 0; mmf_vi_get_max_size(&max_w, &max_h);
	if (img_w <= 0 || img_h <= 0) { img_w = max_w; img_h = max_h; }
	mmf_enc_h265_init(venc_ch, img_w, img_h);
	if (mmf_vi_init() != 0) { mmf_deinit(); rtsp_server_deinit(); return -1; }
	int vi_ch = mmf_get_vi_unused_channel();
	if (mmf_add_vi_channel(vi_ch, img_w, img_h, PIXEL_FORMAT_NV21) != 0) {
		fprintf(stderr, "mmf_add_vi_channel failed\n"); mmf_vi_deinit(); mmf_deinit(); rtsp_server_deinit(); return -1;
	}
	mmf_vi_set_pop_timeout(100);

	void *data; int size, width, height, format;
	for (int i = 0; i < 10 && !g_exit; i++) {
		if (mmf_vi_frame_pop(vi_ch, &data, &size, &width, &height, &format) == 0) mmf_vi_frame_free(vi_ch);
		else usleep(20 * 1000);
	}
	printf("warmup done, streaming...\n");

	/* ---- 闭环状态 ---- */
	om_ctx_t ctx;
	memset(&ctx, 0, sizeof ctx);
	ctx.hmin = 60; ctx.hmax = 91; ctx.smin = 40; ctx.smax = 170; ctx.vmin = 55; ctx.vmax = 170;
	ctx.min_area = 20; ctx.kp = 0.5; ctx.max_delta = 300;
	ctx.alpha = 0.35; ctx.hold = 12; ctx.hold_decay = 0.80;

	om_state_t st; memset(&st, 0, sizeof st);

	msp_parser_t rx; msp_parser_init(&rx);
	int tel_ok = 0, rx_bytes = 0;
	int fc_roll = 0, fc_pitch = 0, fc_yaw = 0, fc_ax = 0, fc_ay = 0, fc_az = 0;

	uint16_t ch[16];
	for (int i = 0; i < 16; i++) ch[i] = 1500;
	ch[2] = 1000;   /* 安全: 油门=1000 不转 */

	uint64_t last = now_ms(), timestamp = 0;
	uint8_t *acc = NULL; int acc_cap = 0;
	int fps_frames = 0; uint64_t fps_t0 = last;
	int seq = 0;

	while (!g_exit) {
		if (mmf_vi_frame_pop(vi_ch, &data, &size, &width, &height, &format) != 0) { usleep(5 * 1000); continue; }
		int w = width > 0 ? width : img_w, h = height > 0 ? height : img_h;

		/* 用上一帧遥测 + 当前 RC 填充显示状态 */
		st.fc_roll = fc_roll; st.fc_pitch = fc_pitch; st.fc_yaw = fc_yaw;
		st.fc_ax = fc_ax; st.fc_ay = fc_ay; st.fc_az = fc_az; st.tel_ok = tel_ok > 0;
		st.rc_roll = ch[0]; st.rc_pitch = ch[1]; st.rc_thr = ch[2];

		/* C++: 检测 + 平滑 + 画框, 就地改 NV21 的 Y 平面 */
		int send_target = om_process((uint8_t *)data, w, h, &st, &ctx);

		/* offset -> RC */
		if (send_target) {
			uint16_t rc4[4];
			msp_offset_to_rc((int)ctx.sx, (int)ctx.sy, w, h, ctx.kp, ctx.max_delta, rc4);
			ch[0] = rc4[0]; ch[1] = rc4[1]; ch[2] = 1000; ch[3] = 1500;
		} else { ch[0] = 1500; ch[1] = 1500; ch[3] = 1500; }

		/* MSP 下发 + 读遥测 */
		if (use_msp) {
			msp_send_set_raw_rc(mfd, ch, 16);
			msp_send_request(mfd, MSP_ATTITUDE);
			msp_send_request(mfd, MSP_RAW_IMU);
			fd_set rf; FD_ZERO(&rf); FD_SET(mfd, &rf);
			struct timeval tv = {0, 6000};
			if (select(mfd + 1, &rf, NULL, NULL, &tv) > 0) {
				uint8_t b[512], dir, cmd, pl[256], len;
				int n = (int)read(mfd, b, sizeof(b));
				if (n > 0) rx_bytes += n;
				for (int i = 0; i < n; i++) {
					if (msp_parser_feed(&rx, b[i], &dir, &cmd, pl, &len)) {
						if (cmd == MSP_ATTITUDE && len >= 6) {
							fc_roll = (int16_t)(pl[0] | (pl[1] << 8));
							fc_pitch = (int16_t)(pl[2] | (pl[3] << 8));
							fc_yaw = (int16_t)(pl[4] | (pl[5] << 8));
							tel_ok++;
						} else if (cmd == MSP_RAW_IMU && len >= 6) {
							fc_ax = (int16_t)(pl[0] | (pl[1] << 8));
							fc_ay = (int16_t)(pl[2] | (pl[3] << 8));
							fc_az = (int16_t)(pl[4] | (pl[5] << 8));
						}
					}
				}
			}
		}

		/* VENC -> RTSP */
		if (mmf_venc_push(venc_ch, (uint8_t *)data, w, h, PIXEL_FORMAT_NV21) == 0) {
			mmf_stream_t stream;
			if (mmf_venc_pop(venc_ch, &stream) == 0) {
				int total = 0;
				for (int i = 0; i < stream.count; i++) total += stream.data_size[i];
				if (total > 0) {
					if (stream.count == 1) rtsp_send_memory_data(timestamp, (uint8_t *)stream.data[0], stream.data_size[0]);
					else {
						if (total > acc_cap) { uint8_t *p = realloc(acc, total); if (p) { acc = p; acc_cap = total; } }
						if (acc) { int off = 0; for (int i = 0; i < stream.count; i++) { memcpy(acc + off, stream.data[i], stream.data_size[i]); off += stream.data_size[i]; } rtsp_send_memory_data(timestamp, acc, off); }
					}
				}
				mmf_venc_free(venc_ch);
			}
		}
		mmf_vi_frame_free(vi_ch);

		uint64_t t = now_ms(); timestamp += t - last; last = t;
		fps_frames++;
		if (t - fps_t0 >= 1000) {
			printf("fps=%d %s off=(%d,%d) RC=(%d,%d) FC=(%.1f,%.1f,%.1f) rx=%d\n",
			       fps_frames, ctx.found ? "TRACK" : (send_target ? "HOLD" : "LOST"),
			       (int)ctx.sx, (int)ctx.sy, ch[0], ch[1],
			       fc_roll / 10.0, fc_pitch / 10.0, fc_yaw / 10.0, rx_bytes);
			fps_frames = 0; fps_t0 = t;
		}
		seq++;
	}

	free(acc);
	if (use_msp) msp_serial_close(mfd);
	mmf_del_venc_channel(venc_ch);
	mmf_del_vi_channel(vi_ch);
	mmf_vi_deinit();
	mmf_deinit();
	rtsp_server_deinit();
	printf("done\n");
	return 0;
}
