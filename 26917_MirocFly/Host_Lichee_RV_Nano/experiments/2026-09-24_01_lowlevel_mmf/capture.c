/*
 * capture.c - MirocFly 上位机抓帧 (底层 MMF API)
 *
 * 管线: CSI sensor -> VI(+ISP) -> CVI_VI_GetChnFrame -> NV21 文件
 * 说明: 板子 ION 余量小, VPSS 建组 OOM, 故跳过 VPSS 直接取 VI 帧;
 *       并把 VI 通道输出改小, 让 VB 池块变小, 给固定的 VI raw DMA 腾空间。
 *
 * 用法: ./capture [chn_w] [chn_h] [frames] [vb_blocks]
 *   输出 capture_<chn_w>x<chn_h>.raw (NV21, 紧密打包)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include "sample_comm.h"
#include "cvi_sys.h"
#include "cvi_vi.h"
#include "cvi_buffer.h"

static volatile int g_exit = 0;
static void sig_handler(int sig) { (void)sig; g_exit = 1; }

static SAMPLE_VI_CONFIG_S stViConfig;
static SAMPLE_INI_CFG_S stIniCfg;

static int save_raw(const char *path, VIDEO_FRAME_INFO_S *frm)
{
	VIDEO_FRAME_S *vf = &frm->stVFrame;
	uint32_t image_size = vf->u32Length[0] + vf->u32Length[1] + vf->u32Length[2];
	FILE *fp = fopen(path, "wb");
	if (!fp) { perror("fopen"); return -1; }

	void *vir = CVI_SYS_Mmap(vf->u64PhyAddr[0], image_size);
	if (!vir) { perror("mmap"); fclose(fp); return -1; }

	uint32_t offset = 0;
	for (int i = 0; i < 3; i++) {
		if (vf->u32Length[i] == 0)
			continue;
		uint8_t *p = (uint8_t *)vir + offset;
		offset += vf->u32Length[i];
		fwrite(p, 1, vf->u32Length[i], fp);
	}
	CVI_SYS_Munmap(vir, image_size);
	fclose(fp);
	printf("saved %s (%u bytes, %dx%d stride=%u)\n", path, offset,
	       vf->u32Width, vf->u32Height, vf->u32Stride[0]);
	return 0;
}

int main(int argc, char *argv[])
{
	int chn_w = 1280, chn_h = 720; /* VI 通道输出尺寸 (可调, 越小越省内存) */
	int frames = 1;
	int vb_blocks = 2;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	if (argc > 1) chn_w = atoi(argv[1]);
	if (argc > 2) chn_h = atoi(argv[2]);
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 4) vb_blocks = atoi(argv[4]);

	printf("=== MirocFly capture (VI direct, no VPSS) ===\n");
	printf("chn=%dx%d frames=%d vb_blocks=%d\n", chn_w, chn_h, frames, vb_blocks);

	/* ---- 1. 从 ini 读传感器配置 ---- */
	if (SAMPLE_COMM_VI_ParseIni(&stIniCfg) != CVI_SUCCESS) {
		fprintf(stderr, "ParseIni failed\n");
		return -1;
	}
	printf("sensor type = %d\n", stIniCfg.enSnsType[0]);

	if (SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, &stViConfig) != CVI_SUCCESS) {
		fprintf(stderr, "IniToViCfg failed\n");
		return -1;
	}
	stViConfig.astViInfo[0].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;

	/* ---- 2. 取传感器尺寸 ---- */
	PIC_SIZE_E enPicSize;
	SIZE_S stSize;
	if (SAMPLE_COMM_VI_GetSizeBySensor(stIniCfg.enSnsType[0], &enPicSize) != CVI_SUCCESS) {
		fprintf(stderr, "GetSizeBySensor failed\n");
		return -1;
	}
	if (SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSize) != CVI_SUCCESS) {
		fprintf(stderr, "GetPicSize failed\n");
		return -1;
	}
	printf("sensor size: %dx%d\n", stSize.u32Width, stSize.u32Height);

	/* ---- 3. SYS init + VB 池 (按 VI 通道输出尺寸算, 不是传感器尺寸) ---- */
	VB_CONFIG_S stVbConf;
	memset(&stVbConf, 0, sizeof(VB_CONFIG_S));
	stVbConf.u32MaxPoolCnt = 1;
	stVbConf.astCommPool[0].u32BlkSize = COMMON_GetPicBufferSize(
		chn_w, chn_h, PIXEL_FORMAT_NV21,
		DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
	stVbConf.astCommPool[0].u32BlkCnt = vb_blocks;
	printf("VB blk size = %u x %u (total %u MB)\n", stVbConf.astCommPool[0].u32BlkSize,
	       stVbConf.astCommPool[0].u32BlkCnt,
	       (stVbConf.astCommPool[0].u32BlkSize * vb_blocks) / (1024 * 1024));

	if (SAMPLE_COMM_SYS_Init(&stVbConf) != CVI_SUCCESS) {
		fprintf(stderr, "SYS_Init failed\n");
		return -1;
	}

	/* ---- 4. VI-VPSS 模式 (仍设 offline, 不建 VPSS 组) ---- */
	VI_VPSS_MODE_S stVIVPSSMode;
	memset(&stVIVPSSMode, 0, sizeof(stVIVPSSMode));
	stVIVPSSMode.aenMode[0] = VI_OFFLINE_VPSS_OFFLINE;
	CVI_SYS_SetVIVPSSMode(&stVIVPSSMode);

	/* ---- 5. VI 启动 ---- */
	VI_DEV ViDev = 0;
	VI_PIPE ViPipe = 0;
	VI_CHN ViChn = 0;
	VI_PIPE_ATTR_S stPipeAttr;
	CVI_S32 s32Ret;

	s32Ret = SAMPLE_COMM_VI_StartSensor(&stViConfig);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "StartSensor failed %#x\n", s32Ret); goto err; }
	s32Ret = SAMPLE_COMM_VI_StartDev(&stViConfig.astViInfo[ViDev]);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "StartDev failed %#x\n", s32Ret); goto err; }
	s32Ret = SAMPLE_COMM_VI_StartMIPI(&stViConfig);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "StartMIPI failed %#x\n", s32Ret); goto err; }

	memset(&stPipeAttr, 0, sizeof(stPipeAttr));
	stPipeAttr.bYuvSkip = CVI_FALSE;
	stPipeAttr.u32MaxW = stSize.u32Width;
	stPipeAttr.u32MaxH = stSize.u32Height;
	stPipeAttr.enPixFmt = PIXEL_FORMAT_RGB_BAYER_12BPP;
	stPipeAttr.enBitWidth = DATA_BITWIDTH_12;
	stPipeAttr.stFrameRate.s32SrcFrameRate = -1;
	stPipeAttr.stFrameRate.s32DstFrameRate = -1;
	stPipeAttr.bNrEn = CVI_TRUE;

	printf("[step] CreatePipe...\n");
	s32Ret = CVI_VI_CreatePipe(ViPipe, &stPipeAttr);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "CreatePipe failed %#x\n", s32Ret); goto err; }
	printf("[step] StartPipe...\n");
	s32Ret = CVI_VI_StartPipe(ViPipe);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "StartPipe failed %#x\n", s32Ret); goto err; }

	printf("[step] StartIsp...\n");
	s32Ret = SAMPLE_COMM_VI_StartIsp(&stViConfig.astViInfo[ViDev]);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "StartIsp failed %#x\n", s32Ret); goto err; }
	printf("[step] StartIsp ok\n");

	/* 自定义 VI 通道: 输出尺寸改小 + depth=1 (省内存, 给固定 raw DMA 腾空间) */
	VI_CHN_ATTR_S stChnAttr;
	SAMPLE_COMM_VI_GetChnAttrBySns(stIniCfg.enSnsType[0], &stChnAttr);
	stChnAttr.stSize.u32Width = chn_w;
	stChnAttr.stSize.u32Height = chn_h;
	stChnAttr.enPixelFormat = PIXEL_FORMAT_NV21;
	stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
	stChnAttr.enDynamicRange = stViConfig.astViInfo[ViDev].stChnInfo.enDynamicRange;
	stChnAttr.enVideoFormat = stViConfig.astViInfo[ViDev].stChnInfo.enVideoFormat;
	stChnAttr.u32Depth = 1;
	printf("[step] SetChnAttr...\n");
	s32Ret = CVI_VI_SetChnAttr(ViPipe, ViChn, &stChnAttr);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "SetChnAttr failed %#x\n", s32Ret); goto err; }
	printf("[step] EnableChn...\n");
	s32Ret = CVI_VI_EnableChn(ViPipe, ViChn);
	if (s32Ret != CVI_SUCCESS) { fprintf(stderr, "EnableChn failed %#x\n", s32Ret); goto err; }
	printf("VI chn enabled at %dx%d depth=1, capturing %d frame(s)...\n", chn_w, chn_h, frames);

	/* ---- 6. 直接从 VI 通道取帧 ---- */
	for (int f = 0; f < frames && !g_exit; f++) {
		VIDEO_FRAME_INFO_S stVideoFrame;
		int got = 0;
		for (int i = 0; i < 100 && !g_exit; i++) {
			if (CVI_VI_GetChnFrame(ViPipe, ViChn, &stVideoFrame, 3000) == CVI_SUCCESS) {
				got = 1;
				break;
			}
			printf("  waiting VI frame %d/100\n", i + 1);
			usleep(50 * 1000);
		}
		if (!got) {
			fprintf(stderr, "VI GetChnFrame failed (no frame)\n");
			break;
		}

		char name[256];
		snprintf(name, sizeof(name), "capture_%ux%u.raw",
			 stVideoFrame.stVFrame.u32Width, stVideoFrame.stVFrame.u32Height);
		save_raw(name, &stVideoFrame);
		CVI_VI_ReleaseChnFrame(ViPipe, ViChn, &stVideoFrame);
	}

	/* ---- 7. 清理 ---- */
	CVI_VI_DisableChn(ViPipe, ViChn);
	CVI_VI_StopPipe(ViPipe);
	CVI_VI_DestroyPipe(ViPipe);
	SAMPLE_COMM_VI_DestroyIsp(&stViConfig);
	SAMPLE_COMM_VI_DestroyVi(&stViConfig);
	SAMPLE_COMM_SYS_Exit();
	printf("done\n");
	return 0;

err:
	SAMPLE_COMM_VI_DestroyIsp(&stViConfig);
	SAMPLE_COMM_VI_DestroyVi(&stViConfig);
	SAMPLE_COMM_SYS_Exit();
	return -1;
}