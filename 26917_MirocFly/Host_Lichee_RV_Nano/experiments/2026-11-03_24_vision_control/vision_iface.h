/*
 * vision_iface.h  —— 视觉端 ↔ 控制端 接口契约 v1
 * ---------------------------------------------------------------------------
 * 冻结日期：2026-11-03（v3 文稿配套）
 *
 * 数据流：视觉端（LicheeRV Nano，YOLO/HSV 检测）
 *            --UDP 127.0.0.1:7788, 每个检测帧一个 vif_frame_t-->
 *         控制端（选目标 / 平滑 / 偏置 / 限幅 / RC）
 *
 * 坐标约定：
 *   - 瞄准量 dx/dy：**相对画面中心**，右正、**上正**（数学习惯）
 *       dx = cx_img - w/2
 *       dy = h/2 - cy_img        （注意：cy_img 向下增大，所以此处取反）
 *   - 框 x/y/w/h：图像习惯，左上原点、y 向下正（仅用于画框/调试）
 *
 * 硬规则：
 *   1) 只给原始像素：不平滑、不加偏置、不转角度或 RC；
 *   2) 无目标 → n=0, primary=-1，**不许保持上一帧**；
 *   3) 每帧带 ts_ns（CLOCK_MONOTONIC）；丢目标 ≤2 帧内输出 n=0。
 *
 * 用法（发送端）：
 *   vif_frame_t f; memset(&f,0,sizeof f);
 *   f.magic=VIF_MAGIC; f.version=VIF_VERSION; f.n=...; f.ts_ns=...;
 *   f.w=1280; f.h=720; f.primary=...;
 *   sendto(sock, &f, sizeof f, 0, &dst, sizeof dst);
 *
 * 用法（接收端）：
 *   recvfrom(...); if (f.magic!=VIF_MAGIC || f.version!=VIF_VERSION) drop;
 *   if (now_ns - f.ts_ns > 300ms) drop;      // 过期数据
 *
 * 注意：sizeof(vif_frame_t) = 152 字节（对齐后）。两端必须用**同一份**本文件。
 */
#ifndef VISION_IFACE_H
#define VISION_IFACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIF_MAGIC    0x56494631u   /* 'VIF1' */
#define VIF_VERSION  1
#define VIF_MAX_DET  8

typedef struct {
	int16_t  dx, dy;      /* 相对画面中心：右正、上正 */
	int16_t  x, y, w, h;  /* 框：左上原点、y 下正（仅调试/画框用） */
	uint16_t area;        /* 轮廓面积 px^2（不是框面积） */
	uint8_t  cls;         /* 0=目标1(1.2~1.4m自旋) 1=目标2(0.4~0.7m) 2=障碍(80cm) */
	uint8_t  conf;        /* 置信度 0-100 */
} vif_det_t;

typedef struct {
	uint32_t  magic;      /* 必须 == VIF_MAGIC */
	uint16_t  version;    /* 必须 == VIF_VERSION */
	uint16_t  n;          /* 有效检测数 0..VIF_MAX_DET */
	uint64_t  ts_ns;      /* 采集时刻，ns，CLOCK_MONOTONIC */
	uint16_t  w, h;       /* 画面尺寸，如 1280,720 */
	int16_t   primary;    /* 推荐关注下标；-1 表示无推荐 */
	vif_det_t det[VIF_MAX_DET];
} vif_frame_t;

/* 编译期尺寸校验：两端不一致会在这里报错，而不是在飞行中 */
#if defined(__cplusplus)
static_assert(sizeof(vif_det_t)   == 16,  "vif_det_t size mismatch");
static_assert(sizeof(vif_frame_t) == 152, "vif_frame_t size mismatch");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(vif_det_t)   == 16,  "vif_det_t size mismatch");
_Static_assert(sizeof(vif_frame_t) == 152, "vif_frame_t size mismatch");
#endif

#ifdef __cplusplus
}
#endif

#endif /* VISION_IFACE_H */
