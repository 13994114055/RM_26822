/*
 * task_sm.cpp - 实验17: 任务状态机雏形 (干跑: 油门=1000 不转)
 *
 * 整合: 检测(HSV)+EMA平滑+丢检滞回 (14) + MSP闭环/遥测 (14) + 撞击检测 (16)
 *       + 相机俯仰偏置参数 pitch_bias_px (默认0, 将来换带角模具只改它)
 *       + 状态机 IDLE→ARMED→TAKEOFF→SEEK→APPROACH→IMPACT→RECOVER→RTH→LAND→DONE
 *
 * 用法: ./task_sm [w] [h] [frames] [serial_dev] [baud] [pitch_bias_px]
 *   serial_dev 省略 -> PTY 假 FC(纯逻辑可跑); 给定 -> 真串口 /dev/ttyS0
 *   pitch_bias_px: 相机俯仰偏置(像素), 垂直回中基准 = 画面中心 + 该值; 默认 0
 *
 * 安全: 油门恒 1000(不转); 丢检回中。真机联调务必 拔桨->绑绳->短飞。
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
#include <ctime>
#include <cmath>

extern "C" {
#include "camera.h"
#include "msp.h"
}

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static const int DET_W = 640, DET_H = 360;

static double now_s(void) {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- PTY 伪 FC ---- */
struct fc_ctx { int fd; volatile int stop; volatile int count; };
static fc_ctx g_fc = {-1, 0, 0};
static void *fc_reader(void *arg) {
	fc_ctx *c = (fc_ctx *)arg;
	msp_parser_t p; msp_parser_init(&p);
	uint8_t buf[512], dir, cmd, pl[256], len;
	while (!c->stop) {
		fd_set rf; FD_ZERO(&rf); FD_SET(c->fd, &rf);
		struct timeval tv = {0, 100000};
		if (select(c->fd + 1, &rf, NULL, NULL, &tv) <= 0) continue;
		int n = (int)read(c->fd, buf, sizeof(buf));
		for (int i = 0; i < n; i++)
			if (msp_parser_feed(&p, buf[i], &dir, &cmd, pl, &len) && cmd == MSP_SET_RAW_RC)
				c->count++;
	}
	return NULL;
}

enum State { ST_IDLE, ST_ARMED, ST_TAKEOFF, ST_SEEK, ST_APPROACH, ST_IMPACT, ST_RECOVER, ST_RTH, ST_LAND, ST_DONE };
static const char *stname[] = {"IDLE","ARMED","TAKEOFF","SEEK","APPROACH","IMPACT","RECOVER","RTH","LAND","DONE"};

/* 时序(干跑, 秒) */
static const double T_IDLE_ARM = 2.0, T_ARM_TAKE = 2.0, T_TAKEOFF = 2.0;
static const double T_IMPACT = 1.0, T_RECOVER = 1.0, T_RTH = 8.0, T_LAND = 1.0;
static const double T_SEEK_LOST = 3.0;   /* 接近中丢目标超时回 SEEK */

int main(int argc, char *argv[])
{
	int req_w = 1280, req_h = 720, frames = 0;
	const char *serial_dev = nullptr;
	int baud = 230400;
	int pitch_bias_px = 0;
	double area_impact = 1000;   /* 撞击视觉判据: 目标面积(检测分辨率)达到则判撞击 */
	int hmin = 60, hmax = 91, smin = 40, smax = 170, vmin = 55, vmax = 170;
	int min_area = 20;
	double kp = 0.5; int max_delta = 300;
	uint16_t throttle = 1000;                 /* 干跑: 不给油 */
	double alpha = 0.35; int hold = 12; double hold_decay = 0.80;
	double impact_thr = 0.8; int cooldown_ms = 800;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	if (argc > 1) req_w = atoi(argv[1]);
	if (argc > 2) req_h = atoi(argv[2]);
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 4) serial_dev = argv[4];
	if (argc > 5) baud = atoi(argv[5]);
	if (argc > 6) pitch_bias_px = atoi(argv[6]);
	if (argc > 7) area_impact = atof(argv[7]);

	printf("=== MirocFly exp17 task_state_machine ===\n");
	printf("cam %dx%d frames=%d out=%s pitch_bias_px=%d (dry-run, thr=%u)\n",
	       req_w, req_h, frames, serial_dev ? serial_dev : "PTY(mock FC)", pitch_bias_px, throttle);

	/* ---- 链路 ---- */
	int fd = -1; pthread_t fc_tid = 0;
	bool use_pty = (serial_dev == nullptr);
	if (use_pty) {
		int master = posix_openpt(O_RDWR | O_NOCTTY);
		if (master < 0 || grantpt(master) || unlockpt(master)) { fprintf(stderr, "pty failed\n"); return -1; }
		struct termios tio; if (tcgetattr(master, &tio) == 0) { cfmakeraw(&tio); tcsetattr(master, TCSANOW, &tio); }
		char *sl = ptsname(master);
		int slave = msp_serial_open(sl, baud);
		if (slave < 0) { fprintf(stderr, "pty slave failed\n"); return -1; }
		g_fc.fd = master; pthread_create(&fc_tid, NULL, fc_reader, &g_fc);
		fd = slave; printf("PTY slave=%s\n", sl);
	} else {
		fd = msp_serial_open(serial_dev, baud);
		if (fd < 0) { fprintf(stderr, "serial open %s failed\n", serial_dev); return -1; }
		printf("serial %s @ %d opened\n", serial_dev, baud);
	}

	/* ---- 相机 ---- */
	mf_camera_cfg_t cfg; mf_camera_cfg_default(&cfg);
	cfg.width = req_w; cfg.height = req_h;
	if (mf_camera_open(&cfg) != 0) { fprintf(stderr, "camera open failed\n"); return -1; }
	mf_camera_warmup(10);
	printf("camera ready, sensor 0x%x\n", mf_camera_sensor_id());

	/* ---- 状态 ---- */
	uint16_t ch[16]; for (int i = 0; i < 16; i++) ch[i] = 1500; ch[2] = throttle;

	msp_parser_t rx; msp_parser_init(&rx);
	int tel_ok = 0;
	int fc_roll = 0, fc_pitch = 0, fc_yaw = 0, fc_ax = 0, fc_ay = 0, fc_az = 0;
	double acc_base = 0; int have_base = 0, impact_evt = 0; double t_impact = -10;

	double sx = 0, sy = 0, sarea = 0; int tracking = 0, lost = 0, found = 0;
	double t_lost_start = 0;

	State st = ST_IDLE; double t_state = now_s();
	double t0 = now_s();

	int seq = 0, frame_total = 0;
	while (!g_exit) {
		mf_frame_t f;
		if (mf_camera_get_frame(&f, 5000) != 0) break;
		int w = f.width, h = f.height;
		cv::Mat yuv(h * 3 / 2, w, CV_8UC1, (void *)f.data);
		cv::Mat bgr; cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV21);
		mf_camera_frame_free();

		/* 检测 */
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
		int offx = 0, offy = 0; double marea = 0; found = 0;
		if (best >= 0) {
			cv::Moments m = cv::moments(contours[best]);
			if (m.m00 > 0) {
				double kx = (double)w / DET_W, ky = (double)h / DET_H;
				offx = (int)((m.m10 / m.m00 - DET_W / 2.0) * kx);
				offy = (int)((m.m01 / m.m00 - DET_H / 2.0) * ky);
				marea = best_a; found = 1;
			}
		}
		/* EMA 平滑 + 丢检滞回 */
		int send_target = 0;
		if (found) {
			lost = 0;
			if (!tracking) { sx = offx; sy = offy; sarea = marea; tracking = 1; }
			else { sx = alpha*offx + (1-alpha)*sx; sy = alpha*offy + (1-alpha)*sy; sarea = alpha*marea + (1-alpha)*sarea; }
			send_target = 1;
		} else if (tracking) {
			if (++lost > hold) tracking = 0;
			else { sx *= hold_decay; sy *= hold_decay; sarea *= hold_decay; send_target = 1; }
		}

		/* 相机俯仰偏置: 垂直回中基准; yaw/roll 不动 */
		int eff_x = (int)sx;
		int eff_y = (int)sy - pitch_bias_px;

		/* MSP: 下发(下一段) + 读遥测 + 撞击判据 */
		if (!use_pty) {
			msp_send_request(fd, MSP_ATTITUDE);
			msp_send_request(fd, MSP_RAW_IMU);
			fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
			struct timeval tv = {0, 6000};
			if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
				uint8_t b[512], dir, cmd, pl[256], len;
				int n = (int)read(fd, b, sizeof(b));
				for (int i = 0; i < n; i++) {
					if (!msp_parser_feed(&rx, b[i], &dir, &cmd, pl, &len)) continue;
					if (cmd == MSP_ATTITUDE && len >= 6) {
						fc_roll = (int16_t)(pl[0]|(pl[1]<<8)); fc_pitch = (int16_t)(pl[2]|(pl[3]<<8)); fc_yaw = (int16_t)(pl[4]|(pl[5]<<8));
						tel_ok++;
					} else if (cmd == MSP_RAW_IMU && len >= 6) {
						fc_ax = (int16_t)(pl[0]|(pl[1]<<8)); fc_ay = (int16_t)(pl[2]|(pl[3]<<8)); fc_az = (int16_t)(pl[4]|(pl[5]<<8));
					}
				}
			}
			double mag = sqrt((double)fc_ax*fc_ax + (double)fc_ay*fc_ay + (double)fc_az*fc_az);
			if (!have_base) { acc_base = mag; have_base = 1; }
			double ref = acc_base > 0 ? acc_base : 1;
			impact_evt = 0;
			if (mag > 1 && fabs(mag - acc_base) > impact_thr * ref) {
				double t = now_s();
				if ((t - t_impact) * 1000 > cooldown_ms) { impact_evt = 1; t_impact = t; }
				acc_base = mag;
			} else if (fabs(mag - acc_base) < 0.3 * ref) {
				acc_base += 0.02 * (mag - acc_base);
			}
		}

		/* ================= 状态机 ================= */
		double t = now_s(), dt = t - t_state;
		const char *note = "";
		switch (st) {
		case ST_IDLE:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;
			if (dt >= T_IDLE_ARM) { st = ST_ARMED; note = "auto-arm(dry)"; t_state = t; }
			break;
		case ST_ARMED:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;
			if (dt >= T_ARM_TAKE) { st = ST_TAKEOFF; note = "takeoff cmd"; t_state = t; }
			break;
		case ST_TAKEOFF:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;   /* 干跑: 爬升不实际给油 */
			if (dt >= T_TAKEOFF) { st = ST_SEEK; note = "climb done(dry)"; t_state = t; }
			break;
		case ST_SEEK:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;    /* 搜索: 干跑原地 */
			if (send_target) { st = ST_APPROACH; note = "target found"; t_state = t; }
			break;
		case ST_APPROACH: {
			uint16_t rc[4];
			msp_offset_to_rc(eff_x, eff_y, w, h, kp, max_delta, rc);
			ch[0]=rc[0]; ch[1]=rc[1]; ch[2]=throttle; ch[3]=1500;
			int vis_impact = send_target && sarea >= area_impact;
			if (impact_evt || vis_impact) { st = ST_IMPACT; note = vis_impact ? "IMPACT! (area>=thr)" : "IMPACT! (imu spike)"; t_state = t; }
			else if (!send_target) {
				if (t_lost_start == 0) t_lost_start = t;
				else if (t - t_lost_start > T_SEEK_LOST) { st = ST_SEEK; note = "target lost -> SEEK"; t_state = t; t_lost_start = 0; }
			} else t_lost_start = 0;
			break;
		}
		case ST_IMPACT:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;
			if (dt >= T_IMPACT) { st = ST_RECOVER; note = "impact hold done"; t_state = t; }
			break;
		case ST_RECOVER:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;
			if (dt >= T_RECOVER) { st = ST_RTH; note = "recovered -> RTH"; t_state = t; }
			break;
		case ST_RTH:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;    /* 干跑: 返航死推算占位 */
			if (dt >= T_RTH) { st = ST_LAND; note = "home reached(dry)"; t_state = t; }
			break;
		case ST_LAND:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=throttle;
			if (dt >= T_LAND) { st = ST_DONE; note = "landed"; t_state = t; }
			break;
		case ST_DONE:
			ch[0]=ch[1]=ch[3]=1500; ch[2]=1000;
			break;
		}

		/* 下发 */
		msp_send_set_raw_rc(fd, ch, 16);

		if (note[0])
			printf("[t=%6.2f] >>> STATE: %-8s | %s | found=%d eff=(%d,%d) area=%.0f FC(%.1f,%.1f,%.1f)\n",
			       t - t0, stname[st], note, found, eff_x, eff_y, sarea,
			       fc_roll/10.0, fc_pitch/10.0, fc_yaw/10.0);
		if ((seq % 30) == 0 && !note[0])
			printf("[t=%6.2f] %-8s found=%d off=(%d,%d) eff=(%d,%d) area=%.0f RC=(%d,%d) FC(%.1f,%.1f,%.1f)\n",
			       t - t0, stname[st], found, (int)sx, (int)sy, eff_x, eff_y, sarea, ch[0], ch[1],
			       fc_roll/10.0, fc_pitch/10.0, fc_yaw/10.0);

		seq++; frame_total++;
		if (frames > 0 && frame_total >= frames) break;
	}

	mf_camera_close();
	if (use_pty) { g_fc.stop = 1; pthread_join(fc_tid, NULL); }
	msp_serial_close(fd);
	printf("done: frames=%d fc_recv=%d\n", frame_total, g_fc.count);
	return 0;
}
