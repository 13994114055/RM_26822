#ifndef MIROCFLY_VISION_CAMERA_H
#define MIROCFLY_VISION_CAMERA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MirocFly 相机层 (Form A) —— scpcom 公有头编译 + 板上 scpcom 库。
 * 一帧 NV21 数据零拷贝指向中间件缓冲, 用完必须 mf_camera_frame_free()。
 */
typedef struct {
	const void *data;   /* NV21 packed (stride == width) */
	int size;
	int width;
	int height;
	int format;
	uint32_t seq;
} mf_frame_t;

typedef struct {
	int width;      /* 请求输出宽 */
	int height;     /* 请求输出高 */
	int fps;
	int hmirror;    /* 软件朝向开关, 1=启用 (见 difficulty_and_method.md B9) */
	int vflip;      /* 中间件 flag 语义偏反, 具体朝向按实测选 */
} mf_camera_cfg_t;

void mf_camera_cfg_default(mf_camera_cfg_t *cfg);

int  mf_camera_open(const mf_camera_cfg_t *cfg);
void mf_camera_close(void);
bool mf_camera_is_open(void);
int  mf_camera_sensor_id(void);

/* 丢弃 n 个前导帧 (流水线启动帧是黑的, 见 B9) */
int  mf_camera_warmup(int n);

/* 取一帧; timeout_ms < 0 表示不限时。成功返回 0, 之后需 mf_camera_frame_free() */
int  mf_camera_get_frame(mf_frame_t *out, int timeout_ms);
void mf_camera_frame_free(void);

#ifdef __cplusplus
}
#endif

#endif
