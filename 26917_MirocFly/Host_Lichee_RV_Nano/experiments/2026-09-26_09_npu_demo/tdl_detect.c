/*
 * tdl_detect.c - 实验09 步骤2: 用高层 TDL 在 NPU 上做目标检测
 *
 * TDL 帮你做了：读图 → 预处理(letterbox/归一化) → NPU 推理 → 后处理(解码+NMS)。
 * 你只需: CreateHandle → OpenModel → ReadImage → Detection → 读结果。
 *
 * 用法: ./tdl_detect <model.cvimodel> <image.jpg|png>
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "tdl_sdk.h"

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char *argv[])
{
	if (argc < 3) {
		printf("usage: %s <model.cvimodel> <image>\n", argv[0]);
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);

	const char *model_path = argv[1];
	const char *image_path = argv[2];
	const TDLModel model_id = TDL_MODEL_YOLOV8_DET_COCO80;

	TDLHandle handle = TDL_CreateHandle(0);
	if (!handle) { printf("TDL_CreateHandle failed\n"); return 1; }

	int ret = TDL_OpenModel(handle, model_id, model_path, NULL);
	if (ret != 0) { printf("TDL_OpenModel failed ret=%d\n", ret); return 1; }
	TDL_SetModelThreshold(handle, model_id, 0.5f);

	TDLImage image = TDL_ReadImage(image_path);
	if (!image) { printf("TDL_ReadImage failed: %s\n", image_path); return 1; }

	TDLObject obj;
	memset(&obj, 0, sizeof(obj));

	/* 计时(含预处理+推理+后处理) */
	int N = 30;
	double t0 = now_ms();
	for (int i = 0; i < N; i++) {
		memset(&obj, 0, sizeof(obj));
		ret = TDL_Detection(handle, model_id, image, &obj);
		if (i == 0 && ret != 0) { printf("TDL_Detection failed ret=%d\n", ret); return 1; }
		TDL_ReleaseObjectMeta(&obj);
	}
	double dt = (now_ms() - t0) / N;

	/* 最后一帧结果打印 */
	memset(&obj, 0, sizeof(obj));
	TDL_Detection(handle, model_id, image, &obj);
	printf("image=%s  size=%ux%u  objects=%u  avg=%.2f ms/frame (%.1f FPS)\n",
	       image_path, obj.width, obj.height, obj.size, dt, dt > 0 ? 1000.0 / dt : 0);
	for (uint32_t i = 0; i < obj.size; i++) {
		TDLObjectInfo *o = &obj.info[i];
		printf("  [%u] %-16s score=%.3f box=(%.0f,%.0f,%.0f,%.0f)\n",
		       i, o->name, o->score, o->box.x1, o->box.y1, o->box.x2, o->box.y2);
	}
	TDL_ReleaseObjectMeta(&obj);
	TDL_DestroyImage(image);
	TDL_CloseModel(handle, model_id);
	TDL_DestroyHandle(handle);
	printf("done\n");
	return 0;
}
