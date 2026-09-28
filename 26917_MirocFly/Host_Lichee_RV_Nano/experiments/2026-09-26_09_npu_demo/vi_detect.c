/*
 * vi_detect.c - 实验09 步骤3: 相机实时目标检测 (TDL 内部 VI 取帧 + TDL_Detection)
 *
 * 用法: ./vi_detect <model.cvimodel> [width] [height] [frames]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tdl_sdk.h"
#include "sample_utils.h"

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("usage: %s <model.cvimodel> [w] [h] [frames]\n", argv[0]);
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);

	const char *model_path = argv[1];
	int W = argc > 2 ? atoi(argv[2]) : 1280;
	int H = argc > 3 ? atoi(argv[3]) : 720;
	int N = argc > 4 ? atoi(argv[4]) : 100;
	const TDLModel model_id = TDL_MODEL_YOLOV8_DET_COCO80;
	const int chn = 0;

	TDLHandle handle = TDL_CreateHandle(0);
	if (!handle) { printf("TDL_CreateHandle failed\n"); return 1; }

	int ret = InitCamera(handle, W, H, IMAGE_YUV420SP_VU, 3);
	if (ret != 0) { printf("InitCamera failed: %#x\n", ret); return 1; }

	ret = TDL_OpenModel(handle, model_id, model_path, NULL);
	if (ret != 0) { printf("TDL_OpenModel failed: %#x\n", ret); DestoryCamera(handle); return 1; }
	TDL_SetModelThreshold(handle, model_id, 0.5f);

	/* 预热相机 */
	for (int i = 0; i < 10; i++) {
		TDLImage im = GetCameraFrame(handle, chn);
		if (im) { ReleaseCameraFrame(handle, chn); TDL_DestroyImage(im); }
		else usleep(20 * 1000);
	}
	printf("camera ready (%dx%d), detecting %d frames...\n", W, H, N);

	int total_objs = 0;
	double t0 = now_ms();
	for (int i = 0; i < N; i++) {
		TDLImage image = GetCameraFrame(handle, chn);
		if (!image) { usleep(10 * 1000); continue; }

		TDLObject obj;
		memset(&obj, 0, sizeof(obj));
		ret = TDL_Detection(handle, model_id, image, &obj);
		if (ret == 0) {
			total_objs += obj.size;
			if (i % 10 == 0) {
				printf("frame %d: om%d", i, obj.size);
				for (uint32_t k = 0; k < obj.size; k++)
					printf(" [c%d %.2f (%.0f,%.0f,%.0f,%.0f)]",
					       obj.info[k].class_id, obj.info[k].score,
					       obj.info[k].box.x1, obj.info[k].box.y1,
					       obj.info[k].box.x2, obj.info[k].box.y2);
				printf("\n");
			}
			TDL_ReleaseObjectMeta(&obj);
		}
		ReleaseCameraFrame(handle, chn);
		TDL_DestroyImage(image);
	}
	double dt = (now_ms() - t0) / N;
	printf("avg %.2f ms/frame = %.1f FPS (objects total=%d)\n", dt, dt > 0 ? 1000.0 / dt : 0, total_objs);

	TDL_CloseModel(handle, model_id);
	DestoryCamera(handle);
	TDL_DestroyHandle(handle);
	printf("done\n");
	return 0;
}
