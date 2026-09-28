/*
 * npu_hello.c - 实验09 步骤1: 用底层 cviruntime 在 NPU 上跑一个模型
 *
 * 部署推理五步（面试常问）:
 *   1) RegisterModel   加载 .cvimodel
 *   2) GetInputOutputTensors  取输入/输出张量
 *   3) 填输入张量 (预处理: 尺寸/格式/量化)
 *   4) Forward         在 NPU 上前向
 *   5) 读输出张量 (后处理: 解析/argmax/NMS)
 *
 * 用法: ./npu_hello <model.cvimodel> [input.raw]
 *   不带 input.raw 时用 128 填充输入(仅验证通路)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "cviruntime.h"

static const char *fmt_name(CVI_FMT f)
{
	switch (f) {
	case CVI_FMT_FP32: return "FP32";
	case CVI_FMT_INT32: return "INT32";
	case CVI_FMT_UINT32: return "UINT32";
	case CVI_FMT_BF16: return "BF16";
	case CVI_FMT_INT16: return "INT16";
	case CVI_FMT_UINT16: return "UINT16";
	case CVI_FMT_INT8: return "INT8";
	case CVI_FMT_UINT8: return "UINT8";
	default: return "?";
	}
}

static const char *pix_name(CVI_NN_PIXEL_FORMAT_E p)
{
	switch (p) {
	case CVI_NN_PIXEL_RGB_PACKED: return "RGB_PACKED";
	case CVI_NN_PIXEL_BGR_PACKED: return "BGR_PACKED";
	case CVI_NN_PIXEL_RGB_PLANAR: return "RGB_PLANAR";
	case CVI_NN_PIXEL_BGR_PLANAR: return "BGR_PLANAR";
	case CVI_NN_PIXEL_YUV_NV12: return "YUV_NV12";
	case CVI_NN_PIXEL_YUV_NV21: return "YUV_NV21";
	case CVI_NN_PIXEL_GRAYSCALE: return "GRAY";
	case CVI_NN_PIXEL_TENSOR: return "TENSOR";
	default: return "?";
	}
}

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void print_tensor(const char *tag, CVI_TENSOR *t)
{
	printf("%s name=%-16s fmt=%-6s count=%zu mem=%zu qscale=%.6f zp=%d pixel=%s shape=[",
	       tag, CVI_NN_TensorName(t), fmt_name(t->fmt),
	       CVI_NN_TensorCount(t), CVI_NN_TensorSize(t),
	       t->qscale, t->zero_point, pix_name(t->pixel_format));
	for (size_t i = 0; i < t->shape.dim_size; i++)
		printf("%d%s", t->shape.dim[i], i + 1 < t->shape.dim_size ? "," : "");
	printf("]\n");
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("usage: %s <model.cvimodel> [input.raw]\n", argv[0]);
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);

	/* 1) 加载模型 */
	CVI_MODEL_HANDLE model = NULL;
	CVI_RC rc = CVI_NN_RegisterModel(argv[1], &model);
	if (rc != 0) {
		printf("CVI_NN_RegisterModel failed rc=%d\n", rc);
		return 1;
	}
	int32_t maj = 0, min = 0;
	if (CVI_NN_GetModelVersion(model, &maj, &min) == 0)
		printf("model version: %d.%d\n", maj, min);
	printf("model target : %s\n", CVI_NN_GetModelTarget(model));

	/* 2) 取输入/输出张量 */
	CVI_TENSOR *inputs = NULL, *outputs = NULL;
	int32_t in_num = 0, out_num = 0;
	rc = CVI_NN_GetInputOutputTensors(model, &inputs, &in_num, &outputs, &out_num);
	if (rc != 0) {
		printf("GetInputOutputTensors failed rc=%d\n", rc);
		return 1;
	}
	printf("inputs=%d outputs=%d\n", in_num, out_num);
	for (int32_t i = 0; i < in_num; i++)
		print_tensor("IN ", &inputs[i]);
	for (int32_t i = 0; i < out_num; i++)
		print_tensor("OUT", &outputs[i]);

	/* 3) 填输入 */
	CVI_TENSOR *t0 = &inputs[0];
	void *in_ptr = CVI_NN_TensorPtr(t0);
	size_t in_size = CVI_NN_TensorSize(t0);
	if (argc >= 3) {
		FILE *fp = fopen(argv[2], "rb");
		if (!fp) {
			printf("open %s failed\n", argv[2]);
			return 1;
		}
		size_t n = fread(in_ptr, 1, in_size, fp);
		fclose(fp);
		printf("fed input: %zu/%zu bytes from %s\n", n, in_size, argv[2]);
	} else {
		memset(in_ptr, 128, in_size);
		printf("no input file -> filled input with 128 (仅验证通路)\n");
	}

	/* 4) 前向 + 计时 */
	for (int i = 0; i < 3; i++)
		CVI_NN_Forward(model, inputs, in_num, outputs, out_num); /* 预热 */
	int N = 100;
	double tstart = now_ms();
	for (int i = 0; i < N; i++)
		rc = CVI_NN_Forward(model, inputs, in_num, outputs, out_num);
	double dt = (now_ms() - tstart) / N;
	printf("CVI_NN_Forward rc=%d : %.3f ms/frame, %.1f FPS\n", rc, dt, dt > 0 ? 1000.0 / dt : 0);

	/* 5) 读输出(以分类为例: argmax) */
	CVI_TENSOR *o0 = &outputs[0];
	size_t cnt = CVI_NN_TensorCount(o0);
	void *optr = CVI_NN_TensorPtr(o0);
	float best = -1e30f;
	int bi = -1;
	if (o0->fmt == CVI_FMT_FP32) {
		float *d = (float *)optr;
		for (size_t i = 0; i < cnt; i++) if (d[i] > best) { best = d[i]; bi = (int)i; }
	} else if (o0->fmt == CVI_FMT_INT8) {
		int8_t *d = (int8_t *)optr;
		for (size_t i = 0; i < cnt; i++) { float v = (d[i] - o0->zero_point) * o0->qscale; if (v > best) { best = v; bi = (int)i; } }
	} else if (o0->fmt == CVI_FMT_UINT8) {
		uint8_t *d = (uint8_t *)optr;
		for (size_t i = 0; i < cnt; i++) { float v = (float)d[i]; if (v > best) { best = v; bi = (int)i; } }
	}
	printf("output0: count=%zu fmt=%s top1_index=%d score=%.4f\n",
	       cnt, fmt_name(o0->fmt), bi, best);

	CVI_NN_CleanupModel(model);
	printf("done\n");
	return 0;
}
