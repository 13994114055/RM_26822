/*
 * overlay_api.h - 实验15 C(C管线) 与 C++(opencv检测/画框) 之间的接口
 */
#ifndef OM_OVERLAY_API_H
#define OM_OVERLAY_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 每帧传给 C++ 的"状态"(来自 FC 遥测 + 当前 RC), 供叠加显示 */
typedef struct {
	int fc_roll, fc_pitch, fc_yaw;   /* 0.1 deg */
	int fc_ax, fc_ay, fc_az;
	int tel_ok;
	int rc_roll, rc_pitch, rc_thr;
	int status;                      /* 0=LOST 1=TRACK 2=HOLD */
} om_state_t;

/* 跨帧上下文(检测参数 + 平滑状态 + 本帧输出) */
typedef struct {
	/* 配置(启动时设定) */
	int hmin, hmax, smin, smax, vmin, vmax, min_area;
	double kp; int max_delta;
	double alpha; int hold; double hold_decay;
	/* 平滑/滞回状态 */
	double sx, sy; int tracking; int lost;
	/* 本帧输出 */
	int found, offx, offy;           /* 原始 offset(全分辨率) */
	int bx1, by1, bx2, by2;          /* 检测框(全分辨率) */
	int sent;                        /* 本帧是否有可下发目标 */
} om_ctx_t;

/* 检测 + 平滑 + 在 nv21 的 Y 平面画叠加; 返回 1=TRACK/HOLD, 0=LOST */
int om_process(uint8_t *nv21, int w, int h, const om_state_t *st, om_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
