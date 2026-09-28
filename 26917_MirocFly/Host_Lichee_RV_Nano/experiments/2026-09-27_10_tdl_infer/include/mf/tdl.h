#ifndef MF_TDL_H
#define MF_TDL_H

/*
 * mf/tdl.h - MirocFly 上位机 TDL 推理层 (Form: 板载 NPU)
 *
 * 对外仅暴露本头; 内部封装 TDL(camera/detect/draw/rtsp), 隐藏第三方细节。
 * 目标: 可直接提升为 libmf_tdl。
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 模型 id (映射到 TDL 内部枚举) */
#define MF_TDL_MODEL_YOLOV8_DET_COCO80  1

typedef struct {
	float x1, y1, x2, y2;
	int   class_id;
	float score;
	char  name[64];
} mf_obj_t;

#define MF_TDL_MAX_OBJ 64
typedef struct {
	int      count;
	int      width, height;
	mf_obj_t objs[MF_TDL_MAX_OBJ];
} mf_objects_t;

/* 不透明帧句柄 */
typedef struct mf_frame mf_frame_t;

/* 模型 */
int  mf_tdl_open(const char *model_path, int model_id);
void mf_tdl_close(void);

/* 相机 (TDL 内部 VI) */
int        mf_tdl_camera_open(int w, int h);
mf_frame_t *mf_tdl_camera_read(int timeout_ms);
void       mf_tdl_camera_release(mf_frame_t *f);

/* 推理 */
int  mf_tdl_detect(mf_frame_t *f, mf_objects_t *out);

/* 可视化: 在帧上画框+标签 */
int  mf_tdl_draw(mf_frame_t *f, const mf_objects_t *objs);

/* RTSP 推流 */
int  mf_tdl_stream_open(int chn, int w, int h);
int  mf_tdl_stream_send(mf_frame_t *f);

#ifdef __cplusplus
}
#endif

#endif
