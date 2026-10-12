/*
 * paradigm.cpp - 实验20: 上位机接管三轴 + 范式选择 + 基础光流阻尼/定点
 *
 * 开关(上位机读 MSP_RC, 内部序 4=ch5 5=ch6 6=ch7 7=ch8):
 *   ch5 = ARM (FC 侧)
 *   接管 ovr = (ch6>1300) || (ch8>1300)     (FC 侧 BOXMSP_RC_OVERRIDE 同条件)
 *   ch6>1300  -> 范式B: 水平用 roll (offx->roll), 垂直 pitch
 *   ch7 [1300,1700) -> 定点(光流速度阻尼+位置积分)
 *   ch7 [1700,2100) -> 定高(留桩)
 *   其它 -> 中位 + 仅基础阻尼
 *   ch8 = ANGLE (FC 侧, >800)
 *
 * 基础层: 接管期间"始终"叠加光流水平速度阻尼(防漂/防摆)。
 *   光流->速度(依 FC 约定 navigation_pos_estimator_flow.c):
 *     v_右 = +(flowRateX-bodyRateX)*(pi/180)*alt
 *     v_前 = -(flowRateY-bodyRateY)*(pi/180)*alt
 *   阻尼: 右移->左滚(roll<1500), 前移->抬头(pitch>1500)
 *
 * 用法: ./paradigm [mode=run|dry|mon] [w] [h] [frames] [serial_dev] [baud=230400]
 *   dry = 只算不发 RC(静态验证); mon = 只读光流/RC(无相机); 省略 serial_dev -> PTY
 * env: MIROCFLY_KD(速度增益) KPP(位置增益) POSLEAK MAXDELTA ALPHA ALT_M SX SY SECS
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
#include <cmath>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>

extern "C" {
#include "camera.h"
#include "msp.h"
}
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }
static int g_dry = 0;
static double g_secs = 0;
static const int DET_W = 640, DET_H = 360;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double envd(const char *n, double d) { const char *v = getenv(n); return v ? atof(v) : d; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct tele {
	int att_r, att_p, att_y;
	int quality, flow_x, flow_y, body_x, body_y;
	long alt_cm;
	uint16_t rc[16]; int nrc;
	int got_att, got_flow, got_alt, got_rc;
};
static msp_parser2_t g_rx;

static void send_reqs(int fd)
{
	msp_send_request(fd, MSP_ATTITUDE);
	msp_send_request(fd, MSP_RC);
	msp2_send_request(fd, MSP2_INAV_OPTICAL_FLOW);
	msp_send_request(fd, MSP_SONAR_ALTITUDE);
}

static int poll_tele(int fd, tele *t, int timeout_ms)
{
	fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
	struct timeval tv = {0, timeout_ms * 1000};
	int frames = 0;
	if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0) return 0;
	uint8_t b[1024], dir, pl[MSP_MAX_PAYLOAD]; uint16_t cmd, len;
	int n = (int)read(fd, b, sizeof(b));
	for (int i = 0; i < n; i++) {
		if (!msp_parser2_feed(&g_rx, b[i], &dir, &cmd, pl, &len)) continue;
		frames++;
		if (cmd == MSP_ATTITUDE && len >= 6) {
			t->att_r = (int16_t)(pl[0] | (pl[1] << 8));
			t->att_p = (int16_t)(pl[2] | (pl[3] << 8));
			t->att_y = (int16_t)(pl[4] | (pl[5] << 8));
			t->got_att = 1;
		} else if (cmd == MSP_RC && len >= 8) {
			int nc = len / 2; if (nc > 16) nc = 16;
			for (int k = 0; k < nc; k++) t->rc[k] = (uint16_t)(pl[2*k] | (pl[2*k+1] << 8));
			t->nrc = nc; t->got_rc = 1;
		} else if (cmd == MSP_SONAR_ALTITUDE && len >= 4) {
			t->alt_cm = (long)(pl[0] | (pl[1] << 8) | (pl[2] << 16) | ((uint32_t)pl[3] << 24));
			t->got_alt = 1;
		} else if (cmd == MSP2_INAV_OPTICAL_FLOW && len >= 9) {
			t->quality = pl[0];
			t->flow_x = (int16_t)(pl[1] | (pl[2] << 8));
			t->flow_y = (int16_t)(pl[3] | (pl[4] << 8));
			t->body_x = (int16_t)(pl[5] | (pl[6] << 8));
			t->body_y = (int16_t)(pl[7] | (pl[8] << 8));
			t->got_flow = 1;
		}
	}
	return frames;
}

static uint16_t ch_get(tele *t, int idx) { return (t->got_rc && idx < t->nrc) ? t->rc[idx] : 0; }

static void send_rc(int fd, int roll, int pitch, int yaw)
{
	uint16_t ch[16];
	for (int i = 0; i < 16; i++) ch[i] = 1500;
	ch[0] = (uint16_t)roll; ch[1] = (uint16_t)pitch; ch[2] = 1000; ch[3] = (uint16_t)yaw;
	if (!g_dry) msp_send_set_raw_rc(fd, ch, 16);
}

/* mon: 只读光流/RC */
static int run_mon(int fd)
{
	printf("=== paradigm mon: 只读(光流/测距/姿态/RC) ===\n");
	msp_parser2_init(&g_rx);
	tele t; memset(&t, 0, sizeof(t));
	double t0 = now_s(), tlog = 0;
	while (!g_exit) {
		send_reqs(fd); poll_tele(fd, &t, 8);
		double now = now_s();
		if (now - tlog >= 0.15) {
			tlog = now;
			printf("[%6.1fs] q=%3d fX=%6d fY=%6d | alt=%ldcm | ch5=%4u ch6=%4u ch7=%4u ch8=%4u | ovr=%d\n",
			       now - t0, t.quality, t.flow_x, t.flow_y, t.alt_cm,
			       ch_get(&t,4), ch_get(&t,5), ch_get(&t,6), ch_get(&t,7),
			       ((ch_get(&t,5)>1300) || (ch_get(&t,7)>1300)) ? 1 : 0);
		}
		usleep(15000);
		if (g_secs > 0 && now - t0 >= g_secs) break;
	}
	return 0;
}

static int run_track(int fd, int req_w, int req_h, int frames)
{
	int hmin = 60, hmax = 91, smin = 40, smax = 170, vmin = 55, vmax = 170, min_area = 20;
	double kpvis = envd("MIROCFLY_KP", 0.4);
	int maxd = (int)envd("MIROCFLY_MAXDELTA", 250);
	double kd = envd("MIROCFLY_KD", 150.0);
	double kpp = envd("MIROCFLY_KPP", 60.0);
	double posleak = envd("MIROCFLY_POSLEAK", 0.995);
	double alpha = envd("MIROCFLY_ALPHA", 0.25);
	double alt_fb = envd("MIROCFLY_ALT_M", 0.3);
	int sx = (int)envd("MIROCFLY_SX", 1), sy = (int)envd("MIROCFLY_SY", 1);
	double ema = 0.35; int hold = 12;   /* 检测平滑/滞回 */

	if (!g_dry) {
		mf_camera_cfg_t cfg; mf_camera_cfg_default(&cfg);
		cfg.width = req_w; cfg.height = req_h;
		if (mf_camera_open(&cfg) != 0) { fprintf(stderr, "camera open failed\n"); return -1; }
		mf_camera_warmup(10);
		printf("camera ready sensor=0x%x\n", mf_camera_sensor_id());
	}
	printf("gains: kpvis=%.2f kd=%.0f kpp=%.0f leak=%.3f alpha=%.2f maxd=%d sx=%d sy=%d%s\n",
	       kpvis, kd, kpp, posleak, alpha, maxd, sx, sy, g_dry ? " [DRY]" : "");

	msp_parser2_init(&g_rx);
	tele t; memset(&t, 0, sizeof(t));
	double sx_o = 0, sy_o = 0; int tracking = 0, lost = 0;
	double vr = 0, vf = 0, pos_r = 0, pos_f = 0;
	int prev_ding = 0;                 /* 上一帧是否定点(用于进入清零) */
	double tprev = now_s(), t0 = tprev, tlog = 0;
	int seq = 0;

	while (!g_exit) {
		int offx = 0, offy = 0, found = 0;
		if (!g_dry || 1) {
			mf_frame_t f;
			if (mf_camera_get_frame(&f, 5000) != 0) break;
			int w = f.width, h = f.height;
			cv::Mat yuv(h * 3 / 2, w, CV_8UC1, (void *)f.data);
			cv::Mat bgr; cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV21);
			mf_camera_frame_free();
			cv::Mat small, hsv, mask;
			cv::resize(bgr, small, cv::Size(DET_W, DET_H));
			cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
			cv::inRange(hsv, cv::Scalar(hmin, smin, vmin), cv::Scalar(hmax, smax, vmax), mask);
			cv::Mat K = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
			cv::morphologyEx(mask, mask, cv::MORPH_OPEN, K);
			cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, K);
			std::vector<std::vector<cv::Point>> cs;
			cv::findContours(mask, cs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
			int best = -1; double ba = 0;
			for (size_t i = 0; i < cs.size(); i++) { double a = cv::contourArea(cs[i]); if (a >= min_area && a > ba) { ba = a; best = (int)i; } }
			if (best >= 0) {
				cv::Moments m = cv::moments(cs[best]);
				if (m.m00 > 0) {
					double kx = (double)w / DET_W, ky = (double)h / DET_H;
					offx = (int)((m.m10 / m.m00 - DET_W / 2.0) * kx);
					offy = (int)((m.m01 / m.m00 - DET_H / 2.0) * ky);
					found = 1;
				}
			}
		}

		/* 检测平滑 + 丢检滞回 */
		if (found) { lost = 0; if (!tracking) { sx_o = offx; sy_o = offy; tracking = 1; } else { sx_o = ema*offx + (1-ema)*sx_o; sy_o = ema*offy + (1-ema)*sy_o; } }
		else if (tracking) { if (++lost > hold) tracking = 0; else { sx_o *= 0.8; sy_o *= 0.8; } }

		/* 遥测 */
		send_reqs(fd); poll_tele(fd, &t, 6);

		int ch6 = ch_get(&t,5), ch7 = ch_get(&t,6), ch8 = ch_get(&t,7);
		int ovr = (ch6 > 1300) || (ch8 > 1300);
		int ding = ovr && (ch7 >= 1300 && ch7 < 1700);   /* 定点 */
		int gao = ovr && (ch7 >= 1700);                  /* 定高 */

		/* 光流 -> 速度(m/s), EMA */
		double altm = (t.alt_cm >= 5) ? t.alt_cm / 100.0 : alt_fb;
		double S = (M_PI / 180.0) * altm;
		int transX = t.flow_x - t.body_x, transY = t.flow_y - t.body_y;
		double ivr = transX * S, ivf = -transY * S;
		if (!t.got_flow) { ivr = 0; ivf = 0; }
		double now = now_s(); double dt = now - tprev; if (dt <= 0) dt = 0.02; tprev = now;
		vr = alpha * ivr + (1 - alpha) * vr;
		vf = alpha * ivf + (1 - alpha) * vf;

		/* 定点: 位置积分 (进入时清零; 泄漏) */
		if (ding && !prev_ding) { pos_r = 0; pos_f = 0; }
		if (ding) { pos_r = posleak * (pos_r + vr * dt); pos_f = posleak * (pos_f + vf * dt); }
		prev_ding = ding;

		/* 基础阻尼(始终, 接管时) */
		double droll = -sx * kd * vr, dpitch = -sy * kd * vf;

		int roll, pitch, yaw = 1500;
		const char *mode;
		if (!ovr) {
			roll = pitch = 1500; mode = "manual";
		} else if (ding) {
			roll = (int)(1500 - sx * (kpp * pos_r + kd * vr));
			pitch = (int)(1500 - sy * (kpp * pos_f + kd * vf));
			mode = "HOLD(定点)";
		} else if (gao) {
			roll = (int)(1500 + droll); pitch = (int)(1500 + dpitch);
			mode = "ALT(定高/桩)";
		} else if (ch6 > 1300) {
			roll = (int)(1500 + kpvis * sx_o + droll);
			pitch = (int)(1500 - kpvis * sy_o + dpitch);
			mode = "B(roll追踪)";
		} else {
			roll = (int)(1500 + droll); pitch = (int)(1500 + dpitch);
			mode = "IDLE(仅阻尼)";
		}
		roll = clampi(roll, 1500 - maxd, 1500 + maxd);
		pitch = clampi(pitch, 1500 - maxd, 1500 + maxd);
		if (!t.got_flow) { /* 光流无效时阻尼/积分归零, 避免乱动 */ }

		send_rc(fd, roll, pitch, yaw);

		if (now - tlog >= 0.15) {
			tlog = now;
			printf("[%6.1fs] %-14s ch6=%4d ch7=%4d | q=%3d tX=%6d tY=%6d alt=%ld | vR=%+.2f vF=%+.2f | %s roll=%4d pitch=%4d\n",
			       now - t0, mode, ch6, ch7, t.quality, transX, transY, t.alt_cm, vr, vf,
			       g_dry ? "[dry]" : "", roll, pitch);
		}
		seq++;
		if (frames > 0 && seq >= frames) break;
		if (g_secs > 0 && now - t0 >= g_secs) break;
	}
	if (!g_dry) mf_camera_close();
	if (!g_dry) send_rc(fd, 1500, 1500, 1500);
	printf("done frames=%d\n", seq);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *mode = argc > 1 ? argv[1] : "run";
	int req_w = 1280, req_h = 720, frames = 0;
	const char *serial_dev = nullptr;
	int baud = 230400;
	if (strcmp(mode, "mon") == 0) {
		/* mon: [mode] [serial_dev] [baud] (无需相机参数) */
		serial_dev = argc > 2 ? argv[2] : nullptr;
		baud = argc > 3 ? atoi(argv[3]) : 230400;
	} else {
		/* run/dry: [mode] [w] [h] [frames] [serial_dev] [baud] */
		req_w = argc > 2 ? atoi(argv[2]) : 1280;
		req_h = argc > 3 ? atoi(argv[3]) : 720;
		frames = argc > 4 ? atoi(argv[4]) : 0;
		serial_dev = argc > 5 ? argv[5] : nullptr;
		baud = argc > 6 ? atoi(argv[6]) : 230400;
	}
	{ const char *s = getenv("MIROCFLY_SERIAL"); if (s) serial_dev = s; }
	{ const char *s = getenv("MIROCFLY_SECS"); if (s) g_secs = atof(s); }
	if (strcmp(mode, "dry") == 0) g_dry = 1;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	printf("=== MirocFly exp20 paradigm (mode=%s%s) ===\n", mode, g_dry ? ",DRY" : "");
	printf("out=%s baud=%d\n", serial_dev ? serial_dev : "PTY(mock FC)", baud);

	int fd = -1; bool use_pty = (serial_dev == nullptr);
	if (use_pty) {
		int master = posix_openpt(O_RDWR | O_NOCTTY);
		if (master < 0 || grantpt(master) || unlockpt(master)) { fprintf(stderr, "pty failed\n"); return -1; }
		struct termios tio; if (tcgetattr(master, &tio) == 0) { cfmakeraw(&tio); tcsetattr(master, TCSANOW, &tio); }
		char *sl = ptsname(master);
		int slave = msp_serial_open(sl, baud);
		if (slave < 0) { fprintf(stderr, "pty slave failed\n"); return -1; }
		fd = slave;
	} else {
		fd = msp_serial_open(serial_dev, baud);
		if (fd < 0) { fprintf(stderr, "serial open %s failed\n", serial_dev); return -1; }
		printf("serial %s @ %d opened\n", serial_dev, baud);
	}

	int rc = 0;
	if (strcmp(mode, "mon") == 0) rc = run_mon(fd);
	else rc = run_track(fd, req_w, req_h, frames);

	msp_serial_close(fd);
	return rc;
}
