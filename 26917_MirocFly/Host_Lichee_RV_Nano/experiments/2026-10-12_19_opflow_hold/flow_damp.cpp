/*
 * flow_damp.cpp - 实验19: 上位机侧"光流水平阻尼/定点"(方法O2)
 *
 * 思路(级联): 下位机 FC 跑内环(姿态), 上位机跑外环——读 FC 的光流+测距+姿态,
 *   估计水平速度, 给 roll/pitch 加"速度阻尼"项来抑制水平漂移。
 *
 * 模式:
 *   mon  : 只读 光流(0x2001) / 测距(58) / 姿态(108) / 电机(104) / RC(105), 打印; 不发控制。
 *          (用于先验证 MTF-02P 光流数据是否可用、轴/符号)
 *   damp : 读光流估水平速度 v, 下发 roll/pitch 阻尼:  cmd = 1500 - kd*v   (符号台架标定)
 *
 * 用法: ./flow_damp [mode=mon] [serial_dev] [baud=230400]
 *   serial_dev 省略 -> 内部 PTY 假 FC。
 * env: MIROCFLY_KD (阻尼增益, 默认 1500)  MIROCFLY_ALT_M (无测距时的兜底高度, 默认 0.3)  MIROCFLY_SECS
 */
#define _GNU_SOURCE
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
#include "msp.h"
}

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }
static double g_secs = 0;
static double now_s(void) {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}
static double envd(const char *n, double d) { const char *v = getenv(n); return v ? atof(v) : d; }

struct tele {
	int att_r, att_p, att_y;      /* 0.1deg */
	int quality;                  /* opflow rawQuality */
	int flow_x, flow_y;           /* deg/s (未去旋转) */
	int body_x, body_y;           /* deg/s (机体系角速度) */
	long alt_cm;                  /* 测距高度 cm */
	int got_flow, got_alt, got_att;
};

static void send_reqs(int fd)
{
	msp2_send_request(fd, MSP2_INAV_OPTICAL_FLOW);
	msp_send_request(fd, MSP_SONAR_ALTITUDE);
	msp_send_request(fd, MSP_ATTITUDE);
	msp_send_request(fd, MSP_RC);
}

static int poll_tele(int fd, msp_parser2_t *p, tele *t, int timeout_ms)
{
	fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
	struct timeval tv = {0, timeout_ms * 1000};
	int frames = 0;
	if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0) return 0;
	uint8_t b[1024], dir, pl[MSP_MAX_PAYLOAD]; uint16_t cmd, len;
	int n = (int)read(fd, b, sizeof(b));
	for (int i = 0; i < n; i++) {
		if (!msp_parser2_feed(p, b[i], &dir, &cmd, pl, &len)) continue;
		frames++;
		if (cmd == MSP_ATTITUDE && len >= 6) {
			t->att_r = (int16_t)(pl[0] | (pl[1] << 8));
			t->att_p = (int16_t)(pl[2] | (pl[3] << 8));
			t->att_y = (int16_t)(pl[4] | (pl[5] << 8));
			t->got_att = 1;
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

/* ---- damp: 光流水平速度阻尼 ----
 * 依据 FC 约定(navigation_pos_estimator_flow.c):
 *   v_right  = + (flowRate[X]-bodyRate[X]) * alt   [rad/s * m = m/s]
 *   v_forward= - (flowRate[Y]-bodyRate[Y]) * alt
 * 阻尼: 右移->左滚(roll<1500); 前移->抬头(pitch>1500)
 *   roll  = 1500 - K*sx*v_right
 *   pitch = 1500 - K*sy*transY*S   (等价, 见下)
 * S = (pi/180)*alt_m ; sx/sy 为可调符号(标定用)
 */
static int clamp_rc(int v){ if(v<1000)v=1000; if(v>2000)v=2000; return v; }

static int run_damp(int fd)
{
	double kd = envd("MIROCFLY_KD", 150.0);       /* RC 单位 / (m/s) */
	int maxd = (int)envd("MIROCFLY_MAXDELTA", 250);
	int sx = (int)envd("MIROCFLY_SX", 1);         /* roll 符号矫正 */
	int sy = (int)envd("MIROCFLY_SY", 1);         /* pitch 符号矫正 */
	double alt_fb = envd("MIROCFLY_ALT_M", 0.3);  /* 无测距兜底高度 */
	double alpha = envd("MIROCFLY_ALPHA", 0.25);  /* 速度 EMA */

	printf("=== flow_damp damp: 光流水平速度阻尼 (不发油门) ===\n");
	printf("kd=%.0f maxd=%d sx=%d sy=%d alt_fb=%.2f alpha=%.2f\n", kd, maxd, sx, sy, alt_fb, alpha);
	printf("前提: ANGLE + 油门>min_check(1100) + ch8 接管\n\n");

	msp_parser2_t p; msp_parser2_init(&p);
	tele t; memset(&t, 0, sizeof(t));
	double t0 = now_s(), tlog = 0, vr = 0, vf = 0;
	long seq = 0;
	while (!g_exit) {
		send_reqs(fd);
		poll_tele(fd, &p, &t, 6);

		double altm = (t.alt_cm >= 5) ? t.alt_cm / 100.0 : alt_fb;
		double S = (M_PI / 180.0) * altm;    /* deg/s -> (m/s)/(deg/s) */
		double inst_vr = (t.flow_x - t.body_x) * S;   /* 右向速度 m/s */
		double inst_vf = -(t.flow_y - t.body_y) * S;  /* 前向速度 m/s */
		if (!t.got_flow) { inst_vr = 0; inst_vf = 0; }
		vr = alpha * inst_vr + (1 - alpha) * vr;
		vf = alpha * inst_vf + (1 - alpha) * vf;

		int roll = clamp_rc((int)(1500 - sx * kd * vr));
		int pitch = clamp_rc((int)(1500 - sy * kd * vf));
		if (roll < 1500 - maxd) roll = 1500 - maxd;
		if (roll > 1500 + maxd) roll = 1500 + maxd;
		if (pitch < 1500 - maxd) pitch = 1500 - maxd;
		if (pitch > 1500 + maxd) pitch = 1500 + maxd;
		/* 若没读到光流, 回中避免乱动 */
		if (!t.got_flow) { roll = 1500; pitch = 1500; }

		uint16_t ch[16];
		for (int i = 0; i < 16; i++) ch[i] = 1500;
		ch[0] = roll; ch[1] = pitch; ch[2] = 1000; ch[3] = 1500;  /* AETR payload; 油门被掩码忽略 */
		msp_send_set_raw_rc(fd, ch, 16);

		double now = now_s();
		if (now - tlog >= 0.15) {
			tlog = now;
			printf("[%6.1fs] q=%3d tX=%6d tY=%6d alt=%ldcm | vR=%+.2f vF=%+.2f | -> roll=%4d pitch=%4d\n",
			       now - t0, t.quality, t.flow_x - t.body_x, t.flow_y - t.body_y,
			       t.alt_cm, vr, vf, roll, pitch);
		}
		usleep(15000);
		seq++;
		if (g_secs > 0 && now - t0 >= g_secs) break;
	}
	/* 退出: 回中(若在真 FC 上则给中性; PTY 无害) */
	{ uint16_t ch[16]; for (int i = 0; i < 16; i++) ch[i] = 1500; msp_send_set_raw_rc(fd, ch, 16); }
	return 0;
}

static int run_mon(int fd)
{
	printf("=== flow_damp mon: 只读光流/测距/姿态 (不发控制) ===\n");
	printf("握住飞机/移动它, 观察 flowX/flowY(=光流) 与 bodyX/bodyY(=机身角速度) 的符号/量级\n\n");
	msp_parser2_t p; msp_parser2_init(&p);
	tele t; memset(&t, 0, sizeof(t));
	double t0 = now_s(), tlog = 0;
	long seq = 0;
	while (!g_exit) {
		send_reqs(fd);
		poll_tele(fd, &p, &t, 8);
		double now = now_s();
		if (now - tlog >= 0.1) {
			tlog = now;
			/* 去旋转后的平移流(deg/s) */
			int tx = t.flow_x - t.body_x, ty = t.flow_y - t.body_y;
			double altm = t.alt_cm > 0 ? t.alt_cm / 100.0 : -1;
			printf("[%6.1fs] q=%3d flowX=%6d flowY=%6d bodyX=%6d bodyY=%6d | transX=%6d transY=%6d | alt=%ldcm(%.2fm) | att r=%6.1f p=%6.1f\n",
			       now - t0, t.quality, t.flow_x, t.flow_y, t.body_x, t.body_y,
			       tx, ty, t.alt_cm, altm, t.att_r / 10.0, t.att_p / 10.0);
		}
		usleep(15000);
		seq++;
		if (g_secs > 0 && now - t0 >= g_secs) break;
	}
	(void)seq;
	return 0;
}

int main(int argc, char *argv[])
{
	const char *mode = argc > 1 ? argv[1] : "mon";
	const char *serial_dev = argc > 2 ? argv[2] : nullptr;
	int baud = argc > 3 ? atoi(argv[3]) : 230400;
	{ const char *s = getenv("MIROCFLY_SERIAL"); if (s) serial_dev = s; }
	{ const char *s = getenv("MIROCFLY_SECS"); if (s) g_secs = atof(s); }

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	printf("=== MirocFly exp19 flow_damp (mode=%s) ===\n", mode);
	printf("out=%s baud=%d\n", serial_dev ? serial_dev : "PTY(mock FC)", baud);

	int fd = -1;
	bool use_pty = (serial_dev == nullptr);
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
	if (strcmp(mode, "damp") == 0) {
		rc = run_damp(fd);
	} else {
		rc = run_mon(fd);
	}
	msp_serial_close(fd);
	return rc;
}
