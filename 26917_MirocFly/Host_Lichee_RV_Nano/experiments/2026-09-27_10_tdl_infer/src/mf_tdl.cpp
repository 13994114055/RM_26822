/*
 * mf_tdl.cpp - mf/tdl.h 的实现: 封装 TDL 的 相机/检测/画框/RTSP。
 */
#include "mf/tdl.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "tdl_sdk.h"
#include "sample_utils.h"
#include "cvi_sys.h"
}

struct mf_frame {
	TDLImage           image;
	VIDEO_FRAME_INFO_S *vf;      /* TDL_WrapImage 输出的是"帧指针"(见官方样例) */
	TDLObject          obj;
	bool               has_obj;
};

static TDLHandle  g_handle = nullptr;
static TDLModel   g_model = TDL_MODEL_INVALID;
static char       g_model_path[512] = {0};
static bool       g_model_opened = false;
static RtspContext g_rtsp = {0};
static bool       g_stream = false;

static TDLModel map_model(int id)
{
	switch (id) {
	case MF_TDL_MODEL_YOLOV8_DET_COCO80: return TDL_MODEL_YOLOV8_DET_COCO80;
	default: return TDL_MODEL_INVALID;
	}
}

int mf_tdl_open(const char *model_path, int model_id)
{
	g_model = map_model(model_id);
	if (g_model == TDL_MODEL_INVALID) {
		fprintf(stderr, "mf_tdl_open: unsupported model_id %d\n", model_id);
		return -1;
	}
	snprintf(g_model_path, sizeof(g_model_path), "%s", model_path);
	g_handle = TDL_CreateHandle(0);
	if (!g_handle) { fprintf(stderr, "TDL_CreateHandle failed\n"); return -1; }
	g_model_opened = false;
	return 0;
}

void mf_tdl_close(void)
{
	if (g_handle) {
		if (g_model_opened)
			TDL_CloseModel(g_handle, g_model);
		DestoryCamera(g_handle);
		TDL_DestroyHandle(g_handle);
		g_handle = nullptr;
		g_model_opened = false;
	}
}

int mf_tdl_camera_open(int w, int h)
{
	if (!g_handle) return -1;
	int ret = InitCamera(g_handle, w, h, IMAGE_YUV420SP_UV, 3);
	if (ret != 0) return -1;
	/* 官方样例顺序: CreateHandle -> InitCamera -> OpenModel */
	ret = TDL_OpenModel(g_handle, g_model, g_model_path, nullptr);
	if (ret != 0) {
		fprintf(stderr, "TDL_OpenModel failed: %#x\n", ret);
		return -1;
	}
	TDL_SetModelThreshold(g_handle, g_model, 0.5f);
	g_model_opened = true;
	return 0;
}

mf_frame_t *mf_tdl_camera_read(int timeout_ms)
{
	if (!g_handle) return nullptr;
	(void)timeout_ms;
	TDLImage image = GetCameraFrame(g_handle, 0);
	if (!image) return nullptr;
	mf_frame_t *f = (mf_frame_t *)calloc(1, sizeof(mf_frame_t));
	if (!f) { TDL_DestroyImage(image); return nullptr; }
	f->image = image;
	f->vf = nullptr;
	TDL_WrapImage(image, &f->vf);
	f->has_obj = false;
	return f;
}

int mf_tdl_detect(mf_frame_t *f, mf_objects_t *out)
{
	if (!f || !out || !g_handle) return -1;
	memset(&f->obj, 0, sizeof(f->obj));
	int ret = TDL_Detection(g_handle, g_model, f->image, &f->obj);
	if (ret != 0) return -1;
	f->has_obj = true;

	memset(out, 0, sizeof(*out));
	out->width = f->obj.width;
	out->height = f->obj.height;
	int n = f->obj.size;
	if (n > MF_TDL_MAX_OBJ) n = MF_TDL_MAX_OBJ;
	out->count = n;
	for (int i = 0; i < n; i++) {
		TDLObjectInfo *o = &f->obj.info[i];
		mf_obj_t *d = &out->objs[i];
		d->x1 = o->box.x1; d->y1 = o->box.y1; d->x2 = o->box.x2; d->y2 = o->box.y2;
		d->class_id = o->class_id;
		d->score = o->score;
		snprintf(d->name, sizeof(d->name), "%s", o->name);
	}
	return 0;
}

static void draw_line_y(uint8_t *y, uint32_t stride, int w, int h,
                        int x0, int y0, int x1, int y1, int t)
{
	for (int k = 0; k < t; k++) {
		if (y0 == y1) {
			int yy = y0 + k;
			if (yy < 0 || yy >= h) continue;
			for (int x = x0; x <= x1; x++)
				if (x >= 0 && x < w) y[yy * stride + x] = 235;
		} else {
			int xx = x0 + k;
			if (xx < 0 || xx >= w) continue;
			for (int yy = y0; yy <= y1; yy++)
				if (yy >= 0 && yy < h) y[yy * stride + xx] = 235;
		}
	}
}

int mf_tdl_draw(mf_frame_t *f, const mf_objects_t *objs)
{
	if (!f || !objs || !f->vf) return -1;
	VIDEO_FRAME_INFO_S *vf = f->vf;
	int w = vf->stVFrame.u32Width, h = vf->stVFrame.u32Height;
	uint32_t stride = vf->stVFrame.u32Stride[0];
	uint32_t len = vf->stVFrame.u32Length[0];
	if (!stride || !len) return -1;

	uint8_t *y = (uint8_t *)CVI_SYS_Mmap(vf->stVFrame.u64PhyAddr[0], len);
	if (!y) return -1;
	CVI_SYS_IonFlushCache(vf->stVFrame.u64PhyAddr[0], y, len);

	for (int i = 0; i < objs->count; i++) {
		const mf_obj_t *o = &objs->objs[i];
		int x1 = (int)o->x1, y1 = (int)o->y1, x2 = (int)o->x2, y2 = (int)o->y2;
		if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
		if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
		int t = 4;
		draw_line_y(y, stride, w, h, x1, y1, x2, y1, t); /* top */
		draw_line_y(y, stride, w, h, x1, y2, x2, y2, t); /* bottom */
		draw_line_y(y, stride, w, h, x1, y1, x1, y2, t); /* left */
		draw_line_y(y, stride, w, h, x2, y1, x2, y2, t); /* right */
	}
	CVI_SYS_IonFlushCache(vf->stVFrame.u64PhyAddr[0], y, len);
	CVI_SYS_Munmap(y, len);
	return 0;
}

int mf_tdl_stream_open(int chn, int w, int h)
{
	g_rtsp.chn = chn;
	g_rtsp.pay_load_type = PT_H265;
	g_rtsp.frame_width = w;
	g_rtsp.frame_height = h;
	g_stream = true;
	return 0;
}

int mf_tdl_stream_send(mf_frame_t *f)
{
	if (!f || !f->vf) return -1;
	return SendFrameRTSP(f->vf, &g_rtsp);
}

void mf_tdl_camera_release(mf_frame_t *f)
{
	if (!f) return;
	if (f->has_obj)
		TDL_ReleaseObjectMeta(&f->obj);
	if (g_handle) {
		ReleaseCameraFrame(g_handle, 0);
	}
	if (f->image)
		TDL_DestroyImage(f->image);
	free(f);
}
