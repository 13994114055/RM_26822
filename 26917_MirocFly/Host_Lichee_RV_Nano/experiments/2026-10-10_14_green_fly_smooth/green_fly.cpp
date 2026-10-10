/*
 * green_fly.cpp - 实验14: 绿色目标闭环 + 检测抖动平滑 (检测 -> 平滑 -> RC -> MSP 注入)
 *
 * 在实验08基础上增加:
 *   - 偏移 EMA 低通 (alpha): 抑制逐帧抖动;
 *   - 丢检滞回/保持 (hold): 单帧漏检不立刻回中, 保持并衰减, 超过 hold 帧才回中;
 *   - 面积平滑 (可选, 仅打印)。
 *
 * 闭环:
 *   VI 帧 -> NV21->BGR -> resize -> HSV -> inRange -> 最大轮廓 -> offset/area
 *     -> [EMA 平滑 + 丢检滞回] -> msp_offset_to_rc() -> SET_RAW_RC (AETR) -> 串口/PTY
 *
 * 用法:
 *   ./green_fly_smooth [w] [h] [frames] [serial_dev] [baud] [alpha] [hold]
 *     serial_dev 省略 -> PTY 模拟 FC; 给定 -> 真串口 (如 /dev/ttyS0 230400)
 *     alpha: EMA 系数 0..1 (默认 0.35, 越小越平滑/越滞后)
 *     hold : 丢检后保持的帧数 (默认 12)
 *   环境变量: MIROCFLY_ALPHA / MIROCFLY_HOLD 可覆盖
 *
 * 安全: 目标丢失(超过 hold) -> 摇杆回中(1500); 油门默认 1000(不转)。
 *       真机联调务必 拔桨->绑绳->短飞。
 */
#define _GNU_SOURCE
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>

extern "C" {
#include "camera.h"
#include "msp.h"
}

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static const int DET_W = 640;
static const int DET_H = 360;

/* ---- PTY 伪 FC: 后台线程收帧并打印收到摇杆 ---- */
struct fc_ctx { int fd; volatile int stop; volatile int count; };
static fc_ctx g_fc = {-1, 0, 0};

static void *fc_reader(void *arg)
{
	fc_ctx *c = (fc_ctx *)arg;
	msp_parser_t p; msp_parser_init(&p);
	uint8_t buf[512], dir, cmd, pl[256], len;
	while (!c->stop) {
		fd_set rf; FD_ZERO(&rf); FD_SET(c->fd, &rf);
		struct timeval tv = {0, 100000};
		int r = select(c->fd + 1, &rf, NULL, NULL, &tv);
		if (r <= 0)
			continue;
		int n = (int)read(c->fd, buf, sizeof(buf));
		for (int i = 0; i < n; i++) {
			if (msp_parser_feed(&p, buf[i], &dir, &cmd, pl, &len)) {
				if (cmd == MSP_SET_RAW_RC && len >= 8) {
					uint16_t roll = pl[0] | (pl[1] << 8);
					uint16_t pitch = pl[2] | (pl[3] << 8);
					uint16_t thr = pl[4] | (pl[5] << 8);
					uint16_t yaw = pl[6] | (pl[7] << 8);
					c->count++;
					if (c->count % 10 == 0)
						printf("[FC] recv SET_RAW_RC roll=%d pitch=%d thr=%d yaw=%d\n",
						       roll, pitch, thr, yaw);
				}
			}
		}
	}
	return NULL;
}

static double envd(const char *name, double def)
{
	const char *v = getenv(name);
	return v ? atof(v) : def;
}

int main(int argc, char *argv[])
{
	int req_w = 1280, req_h = 720, frames = 0;
	const char *serial_dev = nullptr;
	int baud = 230400;                       /* 新板 G473 MSP = 230400 */
	int hmin = 60, hmax = 91, smin = 40, smax = 170, vmin = 55, vmax = 170;
	int min_area = 20;
	double kp = 0.5;
	int max_delta = 300;
	uint16_t throttle = 1000;                /* 安全: 默认不给油 */
	double alpha = envd("MIROCFLY_ALPHA", 0.35);   /* EMA 系数 */
	int hold = (int)envd("MIROCFLY_HOLD", 12);     /* 丢检保持帧数 */
	double hold_decay = 0.80;                       /* 保持期偏移衰减系数 */

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	if (argc > 1) req_w = atoi(argv[1]);
	if (argc > 2) req_h = atoi(argv[2]);
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 4) serial_dev = argv[4];
	if (argc > 5) baud = atoi(argv[5]);
	if (argc > 6) alpha = atof(argv[6]);
	if (argc > 7) hold = atoi(argv[7]);
	if (alpha < 0.0) alpha = 0.0;
	if (alpha > 1.0) alpha = 1.0;

	printf("=== MirocFly exp14 green_fly_smooth ===\n");
	printf("cam %dx%d frames=%d out=%s alpha=%.2f hold=%d\n",
	       req_w, req_h, frames, serial_dev ? serial_dev : "PTY(mock FC)", alpha, hold);

	/* ---- 建立链路 fd ---- */
	int fd = -1;
	pthread_t fc_tid = 0;
	bool use_pty = (serial_dev == nullptr);
	if (use_pty) {
		int master = posix_openpt(O_RDWR | O_NOCTTY);
		if (master < 0 || grantpt(master) || unlockpt(master)) {
			fprintf(stderr, "pty create failed\n");
			return -1;
		}
		struct termios tio;
		if (tcgetattr(master, &tio) == 0) { cfmakeraw(&tio); tcsetattr(master, TCSANOW, &tio); }
		char *sl = ptsname(master);
		int slave = msp_serial_open(sl, baud);
		if (slave < 0) { fprintf(stderr, "pty slave open failed\n"); return -1; }
		g_fc.fd = master;
		pthread_create(&fc_tid, NULL, fc_reader, &g_fc);
		fd = slave;
		printf("PTY slave=%s\n", sl);
	} else {
		fd = msp_serial_open(serial_dev, baud);
		if (fd < 0) { fprintf(stderr, "serial open %s failed\n", serial_dev); return -1; }
		printf("serial %s @ %d opened\n", serial_dev, baud);
	}

	/* ---- 相机 ---- */
	mf_camera_cfg_t cfg;
	mf_camera_cfg_default(&cfg);
	cfg.width = req_w; cfg.height = req_h;
	if (mf_camera_open(&cfg) != 0) { fprintf(stderr, "camera open failed\n"); return -1; }
	mf_camera_warmup(10);
	printf("camera ready, sensor 0x%x\n", mf_camera_sensor_id());

	int seq = 0, found_cnt = 0, sent_cnt = 0;
	uint16_t ch[16];
	for (int i = 0; i < 16; i++) ch[i] = 1500;
	ch[2] = throttle;

	/* ---- FC 遥测回读 (状态反馈) ---- */
	msp_parser_t rx; msp_parser_init(&rx);
	int tel_ok = 0, rx_bytes = 0;
	int fc_roll = 0, fc_pitch = 0, fc_yaw = 0;
	int fc_ax = 0, fc_ay = 0, fc_az = 0;

	/* ---- 平滑/滞回状态 ---- */
	double sx = 0, sy = 0;                 /* EMA 平滑后的偏移 */
	bool tracking = false;                 /* 是否处于跟踪(有可下发目标) */
	int lost = 0;                          /* 连续丢检帧数 */

	while (!g_exit) {
		mf_frame_t f;
		if (mf_camera_get_frame(&f, 5000) != 0) break;
		int w = f.width, h = f.height;
		cv::Mat yuv(h * 3 / 2, w, CV_8UC1, (void *)f.data);
		cv::Mat bgr;
		cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV21);
		mf_camera_frame_free();

		cv::Mat small, hsv, mask;
		cv::resize(bgr, small, cv::Size(DET_W, DET_H));
		cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
		cv::inRange(hsv, cv::Scalar(hmin, smin, vmin), cv::Scalar(hmax, smax, vmax), mask);
		cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
		cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
		cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

		std::vector<std::vector<cv::Point>> contours;
		cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
		int best = -1; double best_a = 0;
		for (size_t i = 0; i < contours.size(); i++) {
			double a = cv::contourArea(contours[i]);
			if (a >= min_area && a > best_a) { best_a = a; best = (int)i; }
		}

		bool found = false;
		int offx = 0, offy = 0;
		if (best >= 0) {
			cv::Moments m = cv::moments(contours[best]);
			if (m.m00 > 0) {
				double scale_x = (double)w / DET_W, scale_y = (double)h / DET_H;
				double cx = m.m10 / m.m00, cy = m.m01 / m.m00;
				offx = (int)((cx - DET_W / 2.0) * scale_x);
				offy = (int)((cy - DET_H / 2.0) * scale_y);
				found = true;
			}
		}

		/* ---- 平滑 + 丢检滞回 ---- */
		bool send_target = false;
		if (found) {
			found_cnt++;
			lost = 0;
			if (!tracking) { sx = offx; sy = offy; tracking = true; }
			else { sx = alpha * offx + (1.0 - alpha) * sx; sy = alpha * offy + (1.0 - alpha) * sy; }
			send_target = true;
		} else {
			if (tracking) {
				lost++;
				if (lost > hold) {
					tracking = false;            /* 真丢失 -> 回中 */
				} else {
					sx *= hold_decay; sy *= hold_decay;  /* 保持期缓慢衰减 */
					send_target = true;
				}
			}
		}

		if (send_target) {
			uint16_t rc[4];
			msp_offset_to_rc((int)sx, (int)sy, w, h, kp, max_delta, rc);
			ch[0] = rc[0]; ch[1] = rc[1]; ch[2] = throttle; ch[3] = 1500;
			if (seq % 10 == 0)
				printf("seq=%d %s raw=(%d,%d) sm=(%d,%d) -> roll=%d pitch=%d%s\n",
				       seq, found ? "TRACK" : "HOLD ", offx, offy, (int)sx, (int)sy,
				       ch[0], ch[1], found ? "" : " (lost)");
		} else {
			ch[0] = 1500; ch[1] = 1500; ch[3] = 1500; /* 回中 */
			if (seq % 30 == 0)
				printf("seq=%d no green -> neutral\n", seq);
		}
		msp_send_set_raw_rc(fd, ch, 16);
		sent_cnt++;

		/* ---- 请求并解析 FC 遥测 (非阻塞) ---- */
		msp_send_request(fd, MSP_ATTITUDE);
		msp_send_request(fd, MSP_RAW_IMU);
		{
			fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
			struct timeval tv = {0, 8000};   /* 8ms, 够 UART2 往返 */
			if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
				uint8_t b[512], dir, cmd, pl[256], len;
				int n = (int)read(fd, b, sizeof(b));
				if (n > 0) { rx_bytes += n; }
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
		if (tel_ok && (seq % 30 == 0))
			printf("  [FC] att roll=%.1f pitch=%.1f yaw=%.1f acc=(%d,%d,%d) rx_bytes=%d\n",
			       fc_roll / 10.0, fc_pitch / 10.0, fc_yaw / 10.0, fc_ax, fc_ay, fc_az, rx_bytes);
		else if (!tel_ok && (seq % 30 == 0))
			printf("  [FC] no telemetry yet (rx_bytes=%d)\n", rx_bytes);

		seq++;
		if (frames > 0 && seq >= frames) break;
	}

	mf_camera_close();
	if (use_pty) { g_fc.stop = 1; pthread_join(fc_tid, NULL); }
	msp_serial_close(fd);
	printf("done: frames=%d found=%d sent=%d fc_recv=%d\n", seq, found_cnt, sent_cnt, g_fc.count);
	return 0;
}
