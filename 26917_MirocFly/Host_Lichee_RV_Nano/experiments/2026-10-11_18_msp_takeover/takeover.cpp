/*
 * takeover.cpp - 实验18: 上位机经 "MSP RC Override" 操控下位机(飞控)行动
 *
 * 目标: HSV 追踪绿色目标 -> 生成 roll/pitch -> MSP_SET_RAW_RC 经 FC 的
 *       MSP RC Override 模式接管 -> 让飞机朝绿目标转/平飞过去。
 *
 * 两种模式:
 *   track  : 相机 HSV 绿目标 -> (offset - pitch_bias) -> EMA 平滑 + 丢检滞回
 *            -> msp_offset_to_rc -> SET_RAW_RC(AETR) -> /dev/ttyS0 @230400
 *   probe  : 无需相机。脚本化小步发 roll± / pitch±，回读 FC 姿态 + 电机输出，
 *            用于台架(压重物/绑绳)判定 "是否真接管 + 方向是否正确 + kp"。
 *
 * ⚠️ 关键(INAV 掩码位序): msp_override_channels 的位 = 内部通道号
 *    ROLL=0 PITCH=1 YAW=2 THROTTLE=3 (AERT!) —— 与 SET_RAW_RC 负载的 AETR 顺序不同。
 *    故 "只接管三轴、油门留飞行员" = 二进制 0111 = 7 (不是 11!)。
 *    (11 = 0b1011 会接管 roll/pitch/throttle 而放开 yaw: payload 油门=1000 会切油!)
 *
 * 用法:
 *   ./takeover [mode] [w] [h] [frames] [serial_dev] [baud] [pitch_bias_px] [kp]
 *   mode     : track | probe (默认 track)
 *   serial_dev 省略 -> 内部 PTY 假 FC(无需硬件可跑全链路); 给定 -> 真串口
 *   pitch_bias_px: 相机俯仰偏置(像素); 垂直回中基准 = 画面中心 + 该值; 默认 0
 *   env: MIROCFLY_KP / MIROCFLY_ALPHA / MIROCFLY_HOLD / MIROCFLY_SERIAL / MIROCFLY_BAUD
 *
 * 安全:
 *   - 油门位 payload 固定 1000(不转); 掩码=7 时该位被 FC 忽略, 油门走物理 CRSF。
 *   - 目标真丢失 -> roll/pitch/yaw 回中(1500)。
 *   - 上位机停发 -> FC 在 failsafe 延时内回落到物理接收机。
 *   - 接管时飞行员须置 ANGLE 模式(ch6 低位), 否则 roll/pitch 是角速度(会自旋)。
 *   - 真机: 拔桨 或 压重物/绑绳 -> 短飞。先跑 probe 定方向/kp, 再 track。
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
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>
#include <ctime>

extern "C" {
#include "camera.h"
#include "msp.h"
}

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static const int DET_W = 640;
static const int DET_H = 360;

/* INAV box: activeBoxIds[0]=BOXARM -> MSP box 掩码 bit0 = 已解锁(可靠)。
 * 其余 box 位按"下发顺序"打包(fc_msp_box.c packBoxModeFlags), 非枚举号, 
 * 故 OVERRIDE 判据改用 MSP_RC 与发送值对比(见 override_match)。 */
static const int BOX_ARM = 0;

static double now_s(void) {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- PTY 伪 FC: 后台线程收帧并计数 ---- */
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
		if (select(c->fd + 1, &rf, NULL, NULL, &tv) <= 0) continue;
		int n = (int)read(c->fd, buf, sizeof(buf));
		for (int i = 0; i < n; i++)
			if (msp_parser_feed(&p, buf[i], &dir, &cmd, pl, &len) && cmd == MSP_SET_RAW_RC)
				c->count++;
	}
	return NULL;
}

/* ---- FC 遥测 ---- */
struct tele {
	int att_r, att_p, att_y;   /* deg*10 */
	int ax, ay, az;
	uint16_t mot[8];
	int rc_r, rc_p, rc_y, rc_t; /* FC 实际在用通道(MSP_RC, override 之后), 内部序 */
	int armed;
	int got_att, got_motor, got_rc, got_box;
};
static msp_parser_t g_rx;

/* 本轮我们发给 FC 的 roll/pitch(用于对比 MSP_RC 判定是否真接管) */
static int g_sent_roll = 1500, g_sent_pitch = 1500;

/* 接管判定: FC 的 roll/pitch(MSP_RC) 与我们发送值接近 -> 接管生效。
 * 返回 1=接管, 0=物理RC, -1=无数据, -2=我们发的是中位(与物理中位不可区分)。
 * 比 ACTIVEBOXES 位掩码可靠(掩码按"下发顺序"打包, 非 box 枚举号)。 */
static int override_match(const tele *t)
{
	if (!t->got_rc) return -1;
	int sr = g_sent_roll - 1500; if (sr < 0) sr = -sr;
	int sp = g_sent_pitch - 1500; if (sp < 0) sp = -sp;
	if (sr <= 50 && sp <= 50) return -2;      /* 中性 -> 无法判定 */
	int dr = t->rc_r - g_sent_roll; if (dr < 0) dr = -dr;
	int dp = t->rc_p - g_sent_pitch; if (dp < 0) dp = -dp;
	return (dr <= 40 && dp <= 40) ? 1 : 0;
}

/* 读一轮 FC 遥测(非阻塞, 阻塞 <=timeout_ms)。返回收到的帧数 */
static int tele_poll(int fd, tele *t, int timeout_ms)
{
	msp_send_request(fd, MSP_ATTITUDE);
	msp_send_request(fd, MSP_RAW_IMU);
	msp_send_request(fd, MSP_MOTOR);
	msp_send_request(fd, MSP_RC);
	msp_send_request(fd, MSP_ACTIVEBOXES);

	fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
	struct timeval tv = {0, timeout_ms * 1000};
	int frames = 0;
	if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
		uint8_t b[1024], dir, cmd, pl[256], len;
		int n = (int)read(fd, b, sizeof(b));
		for (int i = 0; i < n; i++) {
			if (!msp_parser_feed(&g_rx, b[i], &dir, &cmd, pl, &len)) continue;
			frames++;
			if (cmd == MSP_ATTITUDE && len >= 6) {
				t->att_r = (int16_t)(pl[0] | (pl[1] << 8));
				t->att_p = (int16_t)(pl[2] | (pl[3] << 8));
				t->att_y = (int16_t)(pl[4] | (pl[5] << 8));
				t->got_att = 1;
			} else if (cmd == MSP_RAW_IMU && len >= 6) {
				t->ax = (int16_t)(pl[0] | (pl[1] << 8));
				t->ay = (int16_t)(pl[2] | (pl[3] << 8));
				t->az = (int16_t)(pl[4] | (pl[5] << 8));
			} else if (cmd == MSP_MOTOR) {
				int nmot = len / 2; if (nmot > 8) nmot = 8;
				for (int k = 0; k < nmot; k++)
					t->mot[k] = (uint16_t)(pl[2 * k] | (pl[2 * k + 1] << 8));
				t->got_motor = 1;
			} else if (cmd == MSP_RC && len >= 8) {
				t->rc_r = (int16_t)(pl[0] | (pl[1] << 8));
				t->rc_p = (int16_t)(pl[2] | (pl[3] << 8));
				t->rc_y = (int16_t)(pl[4] | (pl[5] << 8));
				t->rc_t = (int16_t)(pl[6] | (pl[7] << 8));
				t->got_rc = 1;
			} else if (cmd == MSP_ACTIVEBOXES && len >= 4) {
				/* bit0 = activeBoxIds[0] = BOXARM(恰好第一个, 可靠); 其余位按"下发顺序", 
				 * 不按 box 枚举号, 故不作为 override 判据(改用 MSP_RC 对比)。 */
				uint32_t w0 = (uint32_t)(pl[0] | (pl[1] << 8) | (pl[2] << 16) | (pl[3] << 24));
				t->armed = w0 & 1u;   /* BOXARM */
				t->got_box = 1;
			}
		}
	}
	return frames;
}

static double envd(const char *name, double def)
{
	const char *v = getenv(name);
	return v ? atof(v) : def;
}

/* 命令下 16 通道 AETR(负载序: [0]roll [1]pitch [2]throttle [3]yaw) */
static void send_rc(int fd, int roll, int pitch, int yaw, int throttle_payload)
{
	uint16_t ch[16];
	for (int i = 0; i < 16; i++) ch[i] = 1500;
	ch[0] = (uint16_t)roll;
	ch[1] = (uint16_t)pitch;
	ch[2] = (uint16_t)throttle_payload;   /* 掩码=7 时被忽略, 走物理油门 */
	ch[3] = (uint16_t)yaw;
	msp_send_set_raw_rc(fd, ch, 16);
}

/* ===================== probe: 方向/接管自检(无相机) ===================== */
static int run_probe(int fd, int step, int frames)
{
	printf("=== probe: 台架方向自检 (油门/模式/拨杆由飞行员给) ===\n");
	printf("前提: 拔桨 或 压重物/绑绳; 飞行员 ARM + ANGLE(ch6低位) + 油门略启 + ch8 抬起(>1700)\n");
	printf("手柄步进=%d; 判据: FCRC(FC实际通道) 与 cmd 一致 -> OVERRIDE OK\n", step);
	printf("⚠️ 油门要给到中小以上(建议 ~1400-1500): 无 AIRMODE 时低油门会停混控 -> 电机不动\n\n");

	struct { const char *name; int roll; int pitch; } seq[] = {
		{"neutral", 1500,           1500},
		{"roll+",   1500 + step,    1500},
		{"neutral", 1500,           1500},
		{"roll-",   1500 - step,    1500},
		{"neutral", 1500,           1500},
		{"pitch+",  1500,           1500 - step},
		{"neutral", 1500,           1500},
		{"pitch-",  1500,           1500 + step},
	};
	const int nseq = (int)(sizeof(seq) / sizeof(seq[0]));
	const double dwell = 1.5;
	int idx = 0, sent = 0, iter = 0;
	double t_phase = now_s(), t_log = 0;

	tele t; memset(&t, 0, sizeof(t));
	double acc[4] = {0, 0, 0, 0}; long accn = 0;   /* 每相位电机累加, 切换时打小结 */
	double neutral_avg[4] = {0, 0, 0, 0}; int have_neutral = 0;

	while (!g_exit) {
		double now = now_s();
		if (now - t_phase >= dwell) {
			if (accn > 0) {
				double avg[4];
				for (int k = 0; k < 4; k++) avg[k] = acc[k] / accn;
				printf("   >> PHASE %-8s avg mot=[%.0f %.0f %.0f %.0f]",
				       seq[idx].name, avg[0], avg[1], avg[2], avg[3]);
				if (strcmp(seq[idx].name, "neutral") == 0) {
					for (int k = 0; k < 4; k++) neutral_avg[k] = avg[k];
					have_neutral = 1;
					printf("  (baseline)\n");
				} else if (have_neutral) {
					printf("  dvs neutral=[%+.0f %+.0f %+.0f %+.0f] up:",
					       avg[0]-neutral_avg[0], avg[1]-neutral_avg[1],
					       avg[2]-neutral_avg[2], avg[3]-neutral_avg[3]);
					for (int k = 0; k < 4; k++)
						if (avg[k] - neutral_avg[k] > 20) printf(" M%d", k + 1);
					printf("  down:");
					for (int k = 0; k < 4; k++)
						if (avg[k] - neutral_avg[k] < -20) printf(" M%d", k + 1);
					printf("\n");
				} else printf("  (no baseline yet)\n");
			}
			for (int k = 0; k < 4; k++) { acc[k] = 0; }
			accn = 0;
			t_phase = now; idx = (idx + 1) % nseq; iter++;
		}

		g_sent_roll = seq[idx].roll; g_sent_pitch = seq[idx].pitch;
		send_rc(fd, seq[idx].roll, seq[idx].pitch, 1500, 1000);
		sent++;

		int fr = tele_poll(fd, &t, 6);
		if (t.got_motor) { for (int k = 0; k < 4; k++) acc[k] += t.mot[k]; accn++; }

		if (now - t_log >= 0.15) {
			t_log = now;
			int ov = override_match(&t);
			const char *ovs = (ov == -1) ? "rc?" : (ov == -2 ? "neutral(n/a)" :
			                  (ov ? "OVERRIDE OK" : (t.armed ? "ARMED/物理RC" : "not-armed/物理RC")));
			printf("[%5.1fs] %-8s cmd(r=%4d p=%4d) | %-14s | FCRC r=%4d p=%4d y=%4d t=%4d | att r=%6.1f p=%6.1f | mot=[%4u %4u %4u %4u] | tiles=%d\n",
			       now - t_phase, seq[idx].name, seq[idx].roll, seq[idx].pitch, ovs,
			       t.got_rc ? t.rc_r : 0, t.got_rc ? t.rc_p : 0, t.got_rc ? t.rc_y : 0, t.got_rc ? t.rc_t : 0,
			       t.att_r / 10.0, t.att_p / 10.0,
			       t.mot[0], t.mot[1], t.mot[2], t.mot[3], fr);
		}
		usleep(20000);
		if (frames > 0 && sent >= frames) break;
	}
	printf("probe done: sent=%d iters=%d\n", sent, iter);
	return 0;
}

/* ===================== track: HSV 绿目标 -> 接管飞行 ===================== */
static int run_track(int fd, int req_w, int req_h, int frames,
                     int pitch_bias_px, double kp, int max_delta)
{
	int hmin = 60, hmax = 91, smin = 40, smax = 170, vmin = 55, vmax = 170;
	int min_area = 20;
	double alpha = envd("MIROCFLY_ALPHA", 0.35);
	int hold = (int)envd("MIROCFLY_HOLD", 12);
	double hold_decay = 0.80;

	mf_camera_cfg_t cfg; mf_camera_cfg_default(&cfg);
	cfg.width = req_w; cfg.height = req_h;
	if (mf_camera_open(&cfg) != 0) { fprintf(stderr, "camera open failed\n"); return -1; }
	mf_camera_warmup(10);
	printf("camera ready, sensor 0x%x (kp=%.2f bias=%d)\n",
	       mf_camera_sensor_id(), kp, pitch_bias_px);

	double sx = 0, sy = 0;
	bool tracking = false;
	int lost = 0, found_cnt = 0, sent = 0, seq = 0;
	tele t; memset(&t, 0, sizeof(t));

	while (!g_exit) {
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

		bool found = false; int offx = 0, offy = 0;
		if (best >= 0) {
			cv::Moments m = cv::moments(contours[best]);
			if (m.m00 > 0) {
				double kx = (double)w / DET_W, ky = (double)h / DET_H;
				offx = (int)((m.m10 / m.m00 - DET_W / 2.0) * kx);
				offy = (int)((m.m01 / m.m00 - DET_H / 2.0) * ky);
				found = true;
			}
		}

		bool send_target = false;
		if (found) {
			found_cnt++; lost = 0;
			if (!tracking) { sx = offx; sy = offy; tracking = true; }
			else { sx = alpha*offx + (1-alpha)*sx; sy = alpha*offy + (1-alpha)*sy; }
			send_target = true;
		} else if (tracking) {
			if (++lost > hold) tracking = false;
			else { sx *= hold_decay; sy *= hold_decay; send_target = true; }
		}

		int roll = 1500, pitch = 1500, yaw = 1500;
		if (send_target) {
			int eff_y = (int)sy - pitch_bias_px;   /* 相机俯仰偏置只作用垂直回中基准 */
			uint16_t rc[4];
			msp_offset_to_rc((int)sx, eff_y, w, h, kp, max_delta, rc);
			roll = rc[0]; pitch = rc[1];
		}
		g_sent_roll = roll; g_sent_pitch = pitch;
		send_rc(fd, roll, pitch, yaw, 1000);
		sent++;

		tele_poll(fd, &t, 6);

		if (seq % 10 == 0) {
			int ov = override_match(&t);
			const char *ovs = (ov == -1) ? "rc?" : (ov == -2 ? "neutral" :
			                  (ov ? "OVERRIDE" : (t.armed ? "物理RC" : "not-armed")));
			printf("seq=%d %-5s off=(%d,%d) sm=(%d,%d) -> RC roll=%d pitch=%d | %-9s | FC att r=%6.1f p=%6.1f\n",
			       seq, found ? "TRACK" : (send_target ? "HOLD" : "LOST"),
			       offx, offy, (int)sx, (int)sy, roll, pitch, ovs,
			       t.att_r / 10.0, t.att_p / 10.0);
		}

		seq++;
		if (frames > 0 && seq >= frames) break;
	}

	mf_camera_close();
	printf("done: frames=%d found=%d sent=%d\n", seq, found_cnt, sent);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *mode = "track";
	int req_w = 1280, req_h = 720, frames = 0;
	const char *serial_dev = nullptr;
	int baud = 230400;
	int pitch_bias_px = 0;
	double kp = envd("MIROCFLY_KP", 0.5);
	int max_delta = 300;
	int probe_step = 200;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	if (argc > 1) mode = argv[1];
	if (argc > 2) req_w = atoi(argv[2]);
	if (argc > 3) req_h = atoi(argv[3]);
	if (argc > 4) frames = atoi(argv[4]);
	if (argc > 5) serial_dev = argv[5];
	if (argc > 6) baud = atoi(argv[6]);
	if (argc > 7) pitch_bias_px = atoi(argv[7]);
	if (argc > 8) kp = atof(argv[8]);
	{ const char *s = getenv("MIROCFLY_SERIAL"); if (s) serial_dev = s; }
	{ const char *b = getenv("MIROCFLY_BAUD"); if (b) baud = atoi(b); }

	printf("=== MirocFly exp18 msp_takeover (mode=%s) ===\n", mode);
	printf("cam %dx%d frames=%d out=%s baud=%d kp=%.2f bias=%d\n",
	       req_w, req_h, frames, serial_dev ? serial_dev : "PTY(mock FC)", baud, kp, pitch_bias_px);

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
	msp_parser_init(&g_rx);

	int rc = 0;
	if (strcmp(mode, "probe") == 0)
		rc = run_probe(fd, probe_step, frames);
	else
		rc = run_track(fd, req_w, req_h, frames, pitch_bias_px, kp, max_delta);

	/* 退出: 回中几帧(若仍接管则给中性; 停发后 FC 回落物理RC) */
	if (!use_pty) { send_rc(fd, 1500, 1500, 1500, 1000); usleep(30000); }
	if (use_pty) { g_fc.stop = 1; pthread_join(fc_tid, NULL); }
	msp_serial_close(fd);
	printf("bye. mock_fc_recv=%d\n", g_fc.count);
	return rc;
}
