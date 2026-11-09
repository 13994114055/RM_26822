/*
 * vision_bridge.c —— 上位机控制端（L0~L4 + 状态机）
 *
 * 架构：
 *   L0  VIO（MAVLink，占位：接口留好，暂不解析）
 *   L1  视觉输入：UDP :7788 收 vif_frame_t（同学 / mock_vision）
 *   L2  目标处理：选目标（锁定+迟滞）→ 丢检保持 → EMA 平滑
 *   L3  控制映射：偏移×kp + bias → 死区 → 限幅 → roll/pitch
 *   L4  执行+权限：查 ACTIVEBOXES 授权 → 只授权时发非中性；MSP → FC
 *
 * 状态机（当前 mask=7，只做 XY；起飞/降落等 mask=15 再加）：
 *   INIT → IDLE → ALIGN → APPROACH → COMMIT → IMPACT → RECOVER → DONE
 *                        ↘ 任意时刻：AUTH_OFF / 超时 → ABORT
 *
 * 用法:
 *   ./vision_bridge [--dry] [--no-fc] [--force-auth] [--dev /dev/ttyACM0] [--baud 230400]
 *
 * env（MIROCFLY_ 前缀）:
 *   KP=0.5 ALPHA=0.35 HOLD=12 MAXDELTA=300 BIAS=0 DEADBAND=8
 *   ROLLSIGN=1 PITCHSIGN=-1 CLS=0 MINCONF=40 UDP=7788
 *   TOL_PX=60 HOLD_MS=500 A_NEAR=6000 A_IMPACT=12000
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#include "vision_iface.h"
#include "msp.h"

/* =====================================================================
 * 0. 基础
 * ===================================================================== */
static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}
static double envd(const char *n, double d) { const char *v = getenv(n); return v ? atof(v) : d; }
static int    envi(const char *n, int d)    { const char *v = getenv(n); return v ? atoi(v) : d; }
static int    clampv(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* =====================================================================
 * 1. 事件（生产者置位，状态机消费）
 * ===================================================================== */
#define EV_TARGET    (1u << 0)   /* 视觉：目标有效且新鲜 */
#define EV_LOST      (1u << 1)   /* 视觉：目标丢失超时   */
#define EV_IMPACT    (1u << 2)   /* 面积跳变 / IMU 冲击  */
#define EV_AUTH_OFF  (1u << 3)   /* 飞手收回授权         */
static volatile uint32_t g_ev = 0;

/* =====================================================================
 * 2. L0：VIO（占位）
 *    真实实现：UART 921600 → mavlink_parse_char → ODOMETRY(331)
 *    这里先留结构，避免后面改状态机接口。
 * ===================================================================== */
typedef struct {
	int    valid;      /* 本帧是否有效 */
	double x, y, z;    /* NED（z 向下 → 高度 = -z） */
	double vx, vy, vz;
	int    quality;
	uint8_t reset_cnt;
} vio_t;
static vio_t g_vio;

static void vio_poll(void)          /* TODO: MAVLink 解析接这里 */
{
	g_vio.valid = 0;                 /* 未接模块：始终无效 */
}

/* =====================================================================
 * 3. L1：视觉输入（UDP）
 * ===================================================================== */
static int g_us = -1;
static vif_frame_t g_frm;
static uint64_t g_frm_ts = 0;
static int g_got_frames = 0;

static void vision_poll(void)
{
	for (;;) {
		vif_frame_t f;
		ssize_t n = recv(g_us, &f, sizeof(f), 0);
		if (n < 0) break;
		if (n != (ssize_t)sizeof(f)) continue;
		if (f.magic != VIF_MAGIC || f.version != VIF_VERSION) continue;
		g_frm = f;
		g_frm_ts = now_ms();
		g_got_frames++;
	}
}

/* =====================================================================
 * 4. L4：FC 链路 + 权限门
 * ===================================================================== */
typedef struct {
	int got_rc, rc[16];
	int got_ab; uint32_t ab;
	int got_ids; int nids; uint8_t ids[32];
	int got_imu; double ax, ay, az;   /* m/s^2 */
	int got_alt; int alt_cm; int vario;  /* MSP_ALTITUDE */
} tele_t;
static tele_t g_t;
static msp_parser_t g_rx;
static int g_fd = -1;

static int g_ovr_pid = 41;   /* INAV: BOXMSPRCOVERRIDE=41（旧固件里 50 是 CHANGEMISSION） */

static int ovr_index(const tele_t *t)
{
	if (!t->got_ids) return -1;
	for (int i = 0; i < t->nids; i++)
		if (t->ids[i] == g_ovr_pid) return i;
	return -1;
}
static int auth_box(const tele_t *t)
{
	int i = ovr_index(t);
	if (i < 0 || !t->got_ab) return -1;
	return (int)((t->ab >> i) & 1u);
}

static void tele_poll(void)
{
	if (g_fd < 0) return;
	msp_send_request(g_fd, MSP_ACTIVEBOXES);
	msp_send_request(g_fd, MSP_BOXIDS);
	msp_send_request(g_fd, MSP_RC);
	msp_send_request(g_fd, MSP_RAW_IMU);
	msp_send_request(g_fd, MSP_ALTITUDE);

	fd_set rf; FD_ZERO(&rf); FD_SET(g_fd, &rf);
	struct timeval tv = { 0, 8000 };
	if (select(g_fd + 1, &rf, NULL, NULL, &tv) <= 0) return;

	uint8_t b[1024], dir, cmd, pl[256], len;
	int n = (int)read(g_fd, b, sizeof(b));
	if (n <= 0) return;
	for (int i = 0; i < n; i++) {
		if (!msp_parser_feed(&g_rx, b[i], &dir, &cmd, pl, &len)) continue;
		if (cmd == MSP_RC && len >= 8) {
			int k = len / 2; if (k > 16) k = 16;
			for (int j = 0; j < k; j++) g_t.rc[j] = (uint16_t)(pl[2*j] | (pl[2*j+1] << 8));
			g_t.got_rc = 1;
		} else if (cmd == MSP_ACTIVEBOXES && len >= 4) {
			g_t.ab = (uint32_t)pl[0] | ((uint32_t)pl[1] << 8) |
			         ((uint32_t)pl[2] << 16) | ((uint32_t)pl[3] << 24);
			g_t.got_ab = 1;
		} else if (cmd == MSP_BOXIDS && len >= 1) {
			int k = len > 32 ? 32 : len;
			memcpy(g_t.ids, pl, (size_t)k); g_t.nids = k; g_t.got_ids = 1;
		} else if (cmd == MSP_ALTITUDE && len >= 6) {
			g_t.alt_cm = (int32_t)((uint32_t)pl[0] | ((uint32_t)pl[1] << 8) |
			                       ((uint32_t)pl[2] << 16) | ((uint32_t)pl[3] << 24));
			g_t.vario = (int16_t)(pl[4] | (pl[5] << 8));
			g_t.got_alt = 1;
		} else if (cmd == MSP_RAW_IMU && len >= 6) {
			/* INAV RAW_IMU: 单位 1/2048 g（与 exp16 一致按原始值处理） */
			g_t.ax = (int16_t)(pl[0] | (pl[1] << 8)) / 2048.0 * 9.80665;
			g_t.ay = (int16_t)(pl[2] | (pl[3] << 8)) / 2048.0 * 9.80665;
			g_t.az = (int16_t)(pl[4] | (pl[5] << 8)) / 2048.0 * 9.80665;
			g_t.got_imu = 1;
		}
	}
}

static void send_rc(int roll, int pitch, int yaw, int thr)
{
	if (g_fd < 0) return;
	uint16_t ch[16];
	for (int i = 0; i < 16; i++) ch[i] = 1500;
	ch[0] = (uint16_t)roll; ch[1] = (uint16_t)pitch;
	ch[2] = (uint16_t)thr;  ch[3] = (uint16_t)yaw;
	msp_send_set_raw_rc(g_fd, ch, 16);
}

/* =====================================================================
 * 5. L2：目标处理
 * ===================================================================== */
typedef struct {
	int    locked, sel, lost;
	double sx, sy;
	double area;
	uint64_t ts;
} tgt_t;
static tgt_t g_tgt;

static double g_kp, g_alpha, g_bias;
static int g_hold, g_maxdelta, g_deadband, g_rollsign, g_pitchsign, g_cls, g_minconf;

static int g_found_cnt = 0;   /* 连续命中帧数（SEARCH 判据） */
static void target_poll(uint32_t *ev)
{
	double age = (g_frm_ts ? (double)(now_ms() - g_frm_ts) : 1e9);
	int fresh = (g_frm_ts && age < 300.0);
	int found = 0;

	if (fresh && g_frm.n > 0) {
		int best = -1;

		/* ① 锁定匹配：找离预测位置最近的同类框（迟滞的核心） */
		if (g_tgt.locked) {
			double bestd = 120.0 * 120.0;
			for (int i = 0; i < g_frm.n; i++) {
				const vif_det_t *d = &g_frm.det[i];
				if (d->cls != g_cls || d->conf < g_minconf) continue;
				double ex = d->dx - g_tgt.sx, ey = d->dy - g_tgt.sy;
				double dd = ex * ex + ey * ey;
				if (dd < bestd) { bestd = dd; best = i; }
			}
		}
		/* ② 否则打分：中心近 + 面积大 */
		if (best < 0) {
			double bs = -1e9;
			for (int i = 0; i < g_frm.n; i++) {
				const vif_det_t *d = &g_frm.det[i];
				if (d->cls != g_cls || d->conf < g_minconf) continue;
				double s = -0.6 * fabs((double)d->dx) / (g_frm.w / 2.0)
				           - 0.3 * fabs((double)d->dy) / (g_frm.h / 2.0)
				           + 0.5 * (d->area / 6000.0);
				if (s > bs) { bs = s; best = i; }
			}
		}
		if (best >= 0) {
			int was = g_tgt.locked;
			g_tgt.sel = best;
			g_tgt.locked = 1;
			g_tgt.lost = 0;
			g_found_cnt++;
			g_tgt.area = g_frm.det[best].area;
			g_tgt.ts = g_frm_ts;
			double dx = g_frm.det[best].dx, dy = g_frm.det[best].dy;
			if (!was) { g_tgt.sx = dx; g_tgt.sy = dy; }
			else {
				g_tgt.sx = g_alpha * dx + (1 - g_alpha) * g_tgt.sx;
				g_tgt.sy = g_alpha * dy + (1 - g_alpha) * g_tgt.sy;
			}
			found = 1;
		}
	}

	if (found) {
		*ev |= EV_TARGET;
	} else {
		g_tgt.lost++;
		g_found_cnt = 0;
		if (g_tgt.lost == 1) *ev |= EV_LOST;
		/* 保持：偏移衰减，超时解锁（与 exp18 一致） */
		if (g_tgt.locked && g_tgt.lost <= g_hold) {
			g_tgt.sx *= 0.80; g_tgt.sy *= 0.80;
			*ev |= EV_TARGET;
		} else if (g_tgt.lost > g_hold) {
			g_tgt.locked = 0; g_tgt.sx = g_tgt.sy = 0;
		}
	}
}

/* =====================================================================
 * 6. L3：映射（yaw 跟随 / pitch 俯仰）
 * ===================================================================== */
static double g_yawkp = 0.6;
static double g_search_yaw = 100;   /* 搜索时固定偏航量(us) */
static int    g_search_ms  = 12000; /* 单向搜索超时 */
static int    g_search_dir = 1;     /* 搜索方向，超时翻转 */
static int    g_alt_air = 30;      /* 离地判据 cm */
static int    g_gate_on = 0;       /* ch8 > 1700 */
static int    g_armed = 0;         /* ACTIVEBOXES bit0 */
static int    g_alt_cm = 0;
static int    g_ch6 = 1500;

/* 垂直：目标居中 + 可选前推 */
static void pitch_cmd(int *pitch, int fwd)
{
	double ey = g_tgt.sy - g_bias;
	if (fabs(ey) < g_deadband) ey = 0;
	*pitch = clampv(1500 + g_pitchsign * (int)(g_kp * ey) - fwd, 1100, 1900);
}

/* 水平：yaw 角速度跟随（dx>0 目标在右 → 右转 → yaw>1500） */
static void yaw_cmd(int *yaw)
{
	*yaw = clampv(1500 + (int)(g_yawkp * g_tgt.sx), 1100, 1900);
}

/* =====================================================================
 * 7. 状态机（权限层 + 任务层）
 *    MONITOR → WAIT_ARM → WAIT_AIR → TRACK_IDLE
 *                                    ├ ch6∈(1300,1700) → ALIGN → APPROACH → COMMIT
 *                                    └ 其他 → 中性
 * ===================================================================== */
typedef enum {
	S_MONITOR, S_WAIT_ARM, S_WAIT_AIR, S_TRACK_IDLE, S_SEARCH,
	S_ALIGN, S_APPROACH, S_COMMIT, S_IMPACT, S_RECOVER, S_COUNT
} st_t;

typedef struct {
	const char *name;
	void (*enter)(void);
	void (*step)(uint32_t ev);
	void (*exit)(void);
} state_def_t;

static st_t g_st = S_MONITOR;
static uint64_t g_t_enter = 0;
static int g_out_r = 1500, g_out_p = 1500, g_out_y = 1500;
static int g_auth = -1, g_dry = 0, g_no_fc = 0;

static double g_tol_px = 60, g_hold_ms = 500;
static uint64_t g_t_center = 0;   /* 连续居中起点 */
static double g_a_near = 6000, g_a_impact = 12000;
static double g_fwd = 80;

static void fsm_set(st_t s);

static int ch6_yaw_mode(void) { return g_ch6 >= 1300 && g_ch6 < 1700; }

static void e_neutral(void) { g_out_r = g_out_p = g_out_y = 1500; }

static void s_monitor(uint32_t ev)
{
	(void)ev;
	if (g_gate_on) fsm_set(S_WAIT_ARM);
}
static void s_wait_arm(uint32_t ev)
{
	(void)ev;
	if (!g_gate_on) { fsm_set(S_MONITOR); return; }
	if (g_armed) fsm_set(S_WAIT_AIR);
}
static void s_wait_air(uint32_t ev)
{
	(void)ev;
	if (!g_gate_on) { fsm_set(S_MONITOR); return; }
	if (!g_armed) { fsm_set(S_WAIT_ARM); return; }
	if (g_alt_cm >= g_alt_air) fsm_set(S_TRACK_IDLE);
}
static void s_track_idle(uint32_t ev)
{
	(void)ev;
	if (!g_gate_on) { fsm_set(S_MONITOR); return; }
	if (!g_armed) { fsm_set(S_WAIT_ARM); return; }
	if (g_alt_cm < 10) { fsm_set(S_WAIT_AIR); return; }   /* 落地了 */
	if (!ch6_yaw_mode()) return;
	if (g_tgt.locked) fsm_set(S_ALIGN);
	else              fsm_set(S_SEARCH);   /* 视野无目标 → 自转搜索 */
}
static void s_search(uint32_t ev)
{
	(void)ev;
	if (!g_gate_on) { fsm_set(S_MONITOR); return; }
	if (!ch6_yaw_mode()) { fsm_set(S_TRACK_IDLE); return; }
	if (g_found_cnt >= 3) { fsm_set(S_ALIGN); return; }   /* 连续 3 帧命中才算找到 */
	g_out_r = 1500;
	g_out_p = 1500;
	g_out_y = clampv(1500 + g_search_dir * (int)g_search_yaw, 1100, 1900);
}

static void e_align(void) { g_t_center = 0; }

static void s_align(uint32_t ev)
{
	if (!g_gate_on) { fsm_set(S_MONITOR); return; }
	if (!ch6_yaw_mode()) { fsm_set(S_TRACK_IDLE); return; }
	if (!(ev & EV_TARGET)) { fsm_set(S_SEARCH); return; }
	int p, y; pitch_cmd(&p, 0); yaw_cmd(&y);
	g_out_p = p; g_out_y = y; g_out_r = 1500;
	int centered = (g_tgt.lost == 0 &&        /* 必须本帧真检到 */
	                fabs(g_tgt.sx) < g_tol_px && fabs(g_tgt.sy) < g_tol_px);
	uint64_t now = now_ms();
	if (centered) {
		if (!g_t_center) g_t_center = now;    /* 开始计时 */
		if (now - g_t_center > (uint64_t)g_hold_ms) fsm_set(S_APPROACH);
	} else {
		g_t_center = 0;                        /* 一旦偏离就重新计时 */
	}
}
static void s_approach(uint32_t ev)
{
	if (!g_gate_on) { fsm_set(S_MONITOR); return; }
	if (!ch6_yaw_mode()) { fsm_set(S_TRACK_IDLE); return; }
	if (!(ev & EV_TARGET)) { fsm_set(S_SEARCH); return; }
	int p, y; pitch_cmd(&p, (int)g_fwd); yaw_cmd(&y);
	g_out_p = p; g_out_y = y; g_out_r = 1500;
	if (g_tgt.area > g_a_impact) { g_ev |= EV_IMPACT; return; }
	if (g_tgt.area > g_a_near) fsm_set(S_COMMIT);
}
static void s_commit(uint32_t ev)
{
	(void)ev;
	if (!g_gate_on) { fsm_set(S_MONITOR); return; }
	if (g_t.got_imu) {
		double am = sqrt(g_t.ax*g_t.ax + g_t.ay*g_t.ay + g_t.az*g_t.az);
		if (am > 3.0 * 9.80665) g_ev |= EV_IMPACT;
	}
	if (g_tgt.area > g_a_impact) g_ev |= EV_IMPACT;
}
static void s_impact(uint32_t ev)
{
	(void)ev;
	g_out_r = g_out_p = g_out_y = 1500;
}
static void s_recover(uint32_t ev)
{
	(void)ev;
	g_out_r = g_out_p = g_out_y = 1500;
}

static const state_def_t g_states[S_COUNT] = {
	[S_MONITOR]    = { "MONITOR",    e_neutral, s_monitor,    NULL },
	[S_WAIT_ARM]   = { "WAIT_ARM",   e_neutral, s_wait_arm,   NULL },
	[S_WAIT_AIR]   = { "WAIT_AIR",   e_neutral, s_wait_air,   NULL },
	[S_TRACK_IDLE] = { "TRACK_IDLE", e_neutral, s_track_idle, NULL },
	[S_SEARCH]     = { "SEARCH",     NULL,      s_search,     NULL },
	[S_ALIGN]      = { "ALIGN",      e_align,   s_align,      NULL },
	[S_APPROACH]   = { "APPROACH",   NULL,      s_approach,   NULL },
	[S_COMMIT]     = { "COMMIT",     NULL,      s_commit,     NULL },
	[S_IMPACT]     = { "IMPACT",     e_neutral, s_impact,     NULL },
	[S_RECOVER]    = { "RECOVER",    e_neutral, s_recover,    NULL },
};

static void fsm_set(st_t s)
{
	if (g_states[g_st].exit) g_states[g_st].exit();
	printf("[%6.2fs] FSM %s -> %s\n",
	       (double)now_ms() / 1000.0, g_states[g_st].name, g_states[s].name);
	g_st = s;
	g_t_enter = now_ms();
	if (g_states[g_st].enter) g_states[g_st].enter();
}

static void fsm_tick(void)
{
	uint32_t ev = g_ev;
	g_ev = 0;

	const state_def_t *d = &g_states[g_st];

	/* 超时（按状态） */
	uint32_t to = 0;
	st_t next = S_TRACK_IDLE;
	switch (g_st) {
	case S_SEARCH:   to = (uint32_t)g_search_ms; next = S_TRACK_IDLE; break;
	case S_ALIGN:    to = 8000; next = S_SEARCH;   break;
	case S_APPROACH: to = 10000; next = S_RECOVER;   break;
	case S_COMMIT:   to = 1500; next = S_RECOVER;    break;
	case S_IMPACT:   to = 1000; next = S_RECOVER;    break;
	case S_RECOVER:  to = 2000; next = S_TRACK_IDLE; break;
	default: break;
	}
	if (to && now_ms() - g_t_enter > to) {
		if (g_st == S_SEARCH) g_search_dir = -g_search_dir;  /* 换个方向扫 */
		fsm_set(next); return;
	}

	if (d->step) d->step(ev);
}

/* =====================================================================
 * 8. main
 * ===================================================================== */
int main(int argc, char *argv[])
{
	const char *dev = getenv("MIROCFLY_DEV");
	int baud = envi("MIROCFLY_BAUD", 230400);
	int udp_port = envi("MIROCFLY_UDP", 7788);
	int force_auth = 0;
	int sim_armed = envi("MIROCFLY_SIM_ARMED", 1);
	int sim_alt   = envi("MIROCFLY_SIM_ALT", 100);
	int sim_aux[4] = { 1000, 1500, 1500, 2000 };   /* ch5..ch8 */

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dry")) g_dry = 1;
		else if (!strcmp(argv[i], "--no-fc")) g_no_fc = 1;
		else if (!strcmp(argv[i], "--force-auth")) force_auth = 1;
		else if (!strcmp(argv[i], "--dev") && i + 1 < argc) dev = argv[++i];
		else if (!strcmp(argv[i], "--baud") && i + 1 < argc) baud = atoi(argv[++i]);
		else { fprintf(stderr, "未知参数 %s\n", argv[i]); return 1; }
	}
	{	/* MIROCFLY_SIM_AUX="1000,1500,1500,2000" */
		const char *v = getenv("MIROCFLY_SIM_AUX");
		if (v) sscanf(v, "%d,%d,%d,%d", &sim_aux[0], &sim_aux[1], &sim_aux[2], &sim_aux[3]);
	}

	g_kp        = envd("MIROCFLY_KP", 0.5);
	g_alpha     = envd("MIROCFLY_ALPHA", 0.35);
	g_hold      = envi("MIROCFLY_HOLD", 12);
	g_maxdelta  = envi("MIROCFLY_MAXDELTA", 300);
	g_bias      = envi("MIROCFLY_BIAS", 0);
	g_deadband  = envi("MIROCFLY_DEADBAND", 8);
	g_rollsign  = envi("MIROCFLY_ROLLSIGN", 1);
	g_pitchsign = envi("MIROCFLY_PITCHSIGN", 1);
	g_cls       = envi("MIROCFLY_CLS", 0);
	g_minconf   = envi("MIROCFLY_MINCONF", 40);
	g_tol_px    = envd("MIROCFLY_TOL_PX", 60);
	g_hold_ms   = envd("MIROCFLY_HOLD_MS", 500);
	g_a_near    = envd("MIROCFLY_A_NEAR", 6000);
	g_a_impact  = envd("MIROCFLY_A_IMPACT", 12000);
	g_fwd       = envd("MIROCFLY_FWD", 80);
	g_yawkp     = envd("MIROCFLY_YAW_KP", 0.6);
	g_search_yaw= envd("MIROCFLY_SEARCH_YAW", 100);
	g_search_ms = envi("MIROCFLY_SEARCH_MS", 12000);
	g_alt_air   = envi("MIROCFLY_ALT_AIR", 30);
	g_ovr_pid   = envi("MIROCFLY_OVR_PID", 41);

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	printf("=== vision_bridge (权限层+任务层) ===\n");
	printf("udp=%d kp=%.2f yawkp=%.2f alpha=%.2f hold=%d maxd=%d bias=%.0f dead=%d\n",
	       udp_port, g_kp, g_yawkp, g_alpha, g_hold, g_maxdelta, g_bias, g_deadband);
	printf("tol=%.0fpx hold=%.0fms A_near=%.0f A_impact=%.0f fwd=%.0f alt_air=%d\n",
	       g_tol_px, g_hold_ms, g_a_near, g_a_impact, g_fwd, g_alt_air);
	printf("search: yaw=%.0fus timeout=%dms reverse-on-timeout\n", g_search_yaw, g_search_ms);
	printf("sim: armed=%d alt=%dcm aux=%d/%d/%d/%d  dry=%d no_fc=%d\n",
	       sim_armed, sim_alt, sim_aux[0], sim_aux[1], sim_aux[2], sim_aux[3], g_dry, g_no_fc);

	g_us = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_us < 0) { perror("socket"); return 1; }
	int one = 1;
	setsockopt(g_us, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET; a.sin_port = htons((uint16_t)udp_port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(g_us, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); return 1; }
	fcntl(g_us, F_SETFL, fcntl(g_us, F_GETFL, 0) | O_NONBLOCK);

	if (!g_no_fc) {
		g_fd = msp_serial_open(dev ? dev : "/dev/ttyACM0", baud);
		if (g_fd < 0) { fprintf(stderr, "打开 FC 失败；用 --no-fc 离线调试\n"); return 1; }
		msp_parser_init(&g_rx);
	}

	memset(&g_t, 0, sizeof(g_t));
	memset(&g_tgt, 0, sizeof(g_tgt));
	fsm_set(S_MONITOR);

	uint64_t t_print = 0, t_poll = 0;
	int auth_last = -2;

	while (!g_exit) {
		uint64_t now = now_ms();

		if (g_fd >= 0 && now - t_poll > 100) { t_poll = now; tele_poll(); }
		vio_poll();   /* L0 占位 */

		/* ---- 权限与模式输入 ---- */
		if (g_no_fc) {
			g_t.rc[4] = (uint16_t)sim_aux[0];
			g_t.rc[5] = (uint16_t)sim_aux[1];
			g_t.rc[6] = (uint16_t)sim_aux[2];
			g_t.rc[7] = (uint16_t)sim_aux[3];
			g_t.got_rc = 1;
			g_armed  = sim_armed;
			g_alt_cm = sim_alt;
		} else {
			g_armed  = (g_t.got_ab && (g_t.ab & 1u));
			g_alt_cm = g_t.alt_cm;
		}
		g_ch6    = g_t.rc[5];
		g_gate_on = (g_t.rc[7] > 1700);

		g_auth = g_no_fc ? (force_auth ? 1 : 0) : auth_box(&g_t);
		if (g_auth != auth_last) {
			auth_last = g_auth;
			printf("[%6.2fs] AUTH %s%s\n", (double)now / 1000.0,
			       g_auth == 1 ? "ON" : (g_auth == 0 ? "OFF(物理RC)" : "未知"),
			       (g_no_fc && force_auth) ? " [force]" : "");
		}

		vision_poll();
		uint32_t ev = 0;
		target_poll(&ev);
		if (g_t.got_imu) {
			double am = sqrt(g_t.ax*g_t.ax + g_t.ay*g_t.ay + g_t.az*g_t.az);
			if (am > 3.0 * 9.80665) ev |= EV_IMPACT;
		}
		g_ev |= ev;

		fsm_tick();

		/* ---- 单一出口 ---- */
		int r = g_out_r, p = g_out_p, y = g_out_y;
		int active = (g_st == S_ALIGN || g_st == S_APPROACH ||
		              g_st == S_COMMIT || g_st == S_SEARCH);
		if (!active) { r = p = y = 1500; }
		if (g_auth != 1 || g_dry || !g_gate_on) { r = p = y = 1500; }
		send_rc(r, p, y, 1000);

		if (now - t_print > 250) {
			t_print = now;
			printf("[%6.2fs] %-9s gate=%d arm=%d alt=%4d ch6=%4d %-4s n=%u "
			       "sm=(%4.0f,%4.0f) out=(%4d,%4d,%4d)\n",
			       (double)now / 1000.0, g_states[g_st].name,
			       g_gate_on, g_armed, g_alt_cm, g_ch6,
			       g_tgt.locked ? (g_tgt.lost ? "HOLD" : "TRK") : "LOST",
			       g_frm.n, g_tgt.sx, g_tgt.sy, r, p, y);
		}
		usleep(20000);
	}

	if (g_fd >= 0) { send_rc(1500, 1500, 1500, 1000); msp_serial_close(g_fd); }
	close(g_us);
	printf("bye. frames=%d\n", g_got_frames);
	return 0;
}
