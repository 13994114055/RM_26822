/*
 * mirovision —— 视觉服务（M1：采集 → 推理 → UDP 发布）
 *
 * 设计要点：
 *   - 相机帧零拷贝：GetCameraFrame 拿到的是 VB 池 buffer 的引用，不 memcpy
 *   - 模型无关：只认 "YOLOv8 + uint8 输入 + INT8"，类别名由 classes.txt 提供
 *   - 对外唯一契约：vif_frame_t（152 字节）UDP
 *   - 无目标也发帧（n=0），让控制端能区分"没目标"和"程序挂了"
 *
 * 用法:
 *   ./mirovision --model best.cvimodel [--classes classes.txt] [--cam 1280x720]
 *                 [--conf 0.50] [--udp 127.0.0.1:7788] [--stats 1] [--frames N]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cmath>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

extern "C" {
#include "cvi_vpss.h"
#include "tdl_sdk.h"
#include "sample_utils.h"
#include "cvi_sys.h"
}

#include "vision_iface.h"

static volatile int g_exit = 0;
static void on_sig(int) { g_exit = 1; }

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static double now_ms(void) { return now_ns() / 1e6; }

static inline int16_t c16(double v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : (int16_t)lround(v)); }
static inline uint16_t u16c(double v) { return v < 0 ? 0 : (v > 65535 ? 65535 : (uint16_t)lround(v)); }

/* 置信度归一：有的版本给 0..1，有的给 0..100 */
static inline uint8_t conf_norm(float s)
{
	double v = (s > 1.0f) ? (double)s : (double)s * 100.0;
	if (v < 0) v = 0;
	if (v > 255) v = 255;
	return (uint8_t)lround(v);
}

/* ---------- 轻量画框（直接改 Y 平面，不依赖 OpenCV） ---------- */
static void y_put(uint8_t *yp, uint32_t stride, int x, int y, int W, int H, uint8_t v)
{
	if (x < 0 || y < 0 || x >= W || y >= H) return;
	yp[(size_t)y * stride + x] = v;
}
static void draw_rect(uint8_t *yp, uint32_t stride, int W, int H, int x1, int y1, int x2, int y2, uint8_t v, int th)
{
	for (int t = 0; t < th; t++) {
		for (int x = x1; x <= x2; x++) { y_put(yp, stride, x, y1 + t, W, H, v); y_put(yp, stride, x, y2 - t, W, H, v); }
		for (int y = y1; y <= y2; y++) { y_put(yp, stride, x1 + t, y, W, H, v); y_put(yp, stride, x2 - t, y, W, H, v); }
	}
}
/* 3x5 点阵数字 0-9 与 '.' */
static const uint8_t g_font[11][5] = {
	{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
	{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},{0,0,0,0,2}
};
static void draw_text(uint8_t *yp, uint32_t stride, int W, int H, int x, int y, const char *t, uint8_t v, int sc)
{
	for (; *t; t++) {
		int idx = (*t >= '0' && *t <= '9') ? (*t - '0') : (*t == '.' ? 10 : -1);
		if (idx < 0) { x += 4 * sc; continue; }
		for (int r = 0; r < 5; r++) {
			uint8_t bits = g_font[idx][r];
			for (int c = 0; c < 3; c++) {
				if (bits & (4 >> c)) {
					for (int dy = 0; dy < sc; dy++)
						for (int dx = 0; dx < sc; dx++)
							y_put(yp, stride, x + c * sc + dx, y + r * sc + dy, W, H, v);
				}
			}
		}
		x += 4 * sc;
	}
}

int main(int argc, char **argv)
{
	const char *model = nullptr, *classes = nullptr, *udp_spec = "127.0.0.1:7788";
	int W = 1280, H = 720, stats_sec = 1, max_frames = 0, rot180 = 0, rtsp_on = 0;
	const char *dump_path = nullptr, *record_path = nullptr;
	FILE *rec = nullptr;
	float conf_th = 0.5f;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--model") && i + 1 < argc) model = argv[++i];
		else if (!strcmp(argv[i], "--classes") && i + 1 < argc) classes = argv[++i];
		else if (!strcmp(argv[i], "--cam") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
		else if (!strcmp(argv[i], "--conf") && i + 1 < argc) conf_th = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--udp") && i + 1 < argc) udp_spec = argv[++i];
		else if (!strcmp(argv[i], "--stats") && i + 1 < argc) stats_sec = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--rot180")) rot180 = 1;
		else if (!strcmp(argv[i], "--rtsp")) rtsp_on = 1;   /* 开 RTSP 叠加（默认关） */
		else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump_path = argv[++i];
		else if (!strcmp(argv[i], "--record") && i + 1 < argc) record_path = argv[++i];
		else { fprintf(stderr, "未知参数 %s\n", argv[i]); return 1; }
	}
	if (!model) {
		fprintf(stderr, "用法: %s --model <cvimodel> [--classes f] [--cam WxH] [--conf 0.5] [--udp ip:port] [--stats 1] [--frames N]\n", argv[0]);
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	/* 类别表：只用来把 class_id 映射成契约里的 cls 序号 */
	static char cls_names[32][32]; int ncls = 0;
	if (classes) {
		FILE *fp = fopen(classes, "r");
		if (fp) {
			while (ncls < 32 && fgets(cls_names[ncls], sizeof(cls_names[0]), fp)) {
				size_t l = strlen(cls_names[ncls]);
				while (l && (cls_names[ncls][l-1] == '\n' || cls_names[ncls][l-1] == '\r')) cls_names[ncls][--l] = 0;
				if (l) ncls++;
			}
			fclose(fp);
		}
	}
	printf("[mirovision] model=%s classes=%s(%d) cam=%dx%d conf=%.2f udp=%s\n",
	       model, classes ? classes : "-", ncls, W, H, conf_th, udp_spec);

	/* UDP */
	char ip[64] = "127.0.0.1"; int port = 7788;
	{ const char *c = strchr(udp_spec, ':'); if (c) { snprintf(ip, sizeof(ip), "%.*s", (int)(c - udp_spec), udp_spec); port = atoi(c + 1); } else snprintf(ip, sizeof(ip), "%s", udp_spec); }
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in dst; memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET; dst.sin_port = htons((uint16_t)port);
	inet_pton(AF_INET, ip, &dst.sin_addr);

	/* TDL：CreateHandle → InitCamera → OpenModel（官方顺序） */
	TDLHandle h = TDL_CreateHandle(0);
	if (!h) { fprintf(stderr, "[mirovision] TDL_CreateHandle failed\n"); return 1; }
	if (InitCamera(h, W, H, IMAGE_YUV420SP_UV, 3) != 0) { fprintf(stderr, "[mirovision] InitCamera failed\n"); TDL_DestroyHandle(h); return 1; }
	RtspContext rtsp_ctx; memset(&rtsp_ctx, 0, sizeof(rtsp_ctx));
	if (rtsp_on) {
		rtsp_ctx.chn = 0;
		rtsp_ctx.pay_load_type = PT_H264;
		rtsp_ctx.frame_width = W;
		rtsp_ctx.frame_height = H;
		printf("[mirovision] rtsp: h264 %dx%d（连接后自动启动编码）\n", W, H);
	}
	if (TDL_OpenModel(h, TDL_MODEL_YOLOV8, model, NULL) != 0) { fprintf(stderr, "[mirovision] TDL_OpenModel failed\n"); DestoryCamera(h); TDL_DestroyHandle(h); return 1; }
	/* 相机倒装：VPSS 硬件 180° 翻转（镜像+翻转），几乎不耗 CPU */
	auto apply_flip = [&]() {
		if (!rot180) return;
		{
		VPSS_CHN_ATTR_S attr;
		if (CVI_VPSS_GetChnAttr(0, 0, &attr) == 0) {
			attr.bMirror = CVI_TRUE; attr.bFlip = CVI_TRUE;
			if (CVI_VPSS_SetChnAttr(0, 0, &attr) == 0) printf("[mirovision] rot180: VPSS 翻转已启用\n");
			else fprintf(stderr, "[mirovision] rot180: SetChnAttr 失败\n");
		} else fprintf(stderr, "[mirovision] rot180: GetChnAttr 失败\n");
		}
	};
	apply_flip();
	TDL_SetModelThreshold(h, TDL_MODEL_YOLOV8, conf_th);
	if (record_path) {
		rec = fopen(record_path, "wb");
		if (rec) printf("[mirovision] record -> %s（每帧 152 字节）\n", record_path);
		else fprintf(stderr, "[mirovision] record 打开失败: %s\n", record_path);
	}
	printf("[mirovision] ready\n");

	/* 统计 */
	uint64_t t0 = now_ms(), t_report = t0;
	long frames = 0, det_frames = 0, det_total = 0;
	double infer_ms_sum = 0, conf_sum = 0; long conf_n = 0;
	int16_t dxmin = 32767, dxmax = -32768, dymin = 32767, dymax = -32768;

	int noframe = 0, infer_fail = 0;
	while (!g_exit) {
		TDLImage img = GetCameraFrame(h, 0);
		if (!img) {
			usleep(2000);
			if (++noframe >= 300) {          /* ≈ 一段时间拿不到帧 */
				fprintf(stderr, "[mirovision] 相机无帧 %d 次，尝试重开\n", noframe);
				DestoryCamera(h);
				usleep(300000);
				if (InitCamera(h, W, H, IMAGE_YUV420SP_UV, 3) == 0) {
					apply_flip();
					fprintf(stderr, "[mirovision] 相机重开成功\n");
				} else {
					fprintf(stderr, "[mirovision] 相机重开失败\n");
				}
				noframe = 0;
			}
			continue;
		}
		noframe = 0;

		double t1 = now_ms();
		TDLObject obj; memset(&obj, 0, sizeof(obj));
		int ok = (TDL_Detection(h, TDL_MODEL_YOLOV8, img, &obj) == 0);
		if (!ok) {
			if (++infer_fail >= 50) {        /* 推理连续失败 → 重载模型 */
				fprintf(stderr, "[mirovision] 推理连续失败 %d 次，重载模型\n", infer_fail);
				TDL_CloseModel(h, TDL_MODEL_YOLOV8);
				usleep(200000);
				if (TDL_OpenModel(h, TDL_MODEL_YOLOV8, model, NULL) == 0) {
					TDL_SetModelThreshold(h, TDL_MODEL_YOLOV8, conf_th);
					fprintf(stderr, "[mirovision] 模型重载成功\n");
				} else {
					fprintf(stderr, "[mirovision] 模型重载失败\n");
				}
				infer_fail = 0;
			}
		} else {
			infer_fail = 0;
		}
		double infer = now_ms() - t1;
		infer_ms_sum += infer; frames++;

		vif_frame_t pkt; memset(&pkt, 0, sizeof(pkt));
		pkt.magic = VIF_MAGIC; pkt.version = VIF_VERSION;
		pkt.ts_ns = now_ns(); pkt.w = (uint16_t)W; pkt.h = (uint16_t)H;
		pkt.primary = -1;

		int n = ok ? (int)obj.size : 0;
		if (n < 0) n = 0;
		if (n > VIF_MAX_DET) n = VIF_MAX_DET;

		int cnt = 0; double best = -1;
		if (n > 0) det_frames++;
		for (int i = 0; i < n; i++) {
			const TDLObjectInfo *o = &obj.info[i];
			double x1 = o->box.x1, y1 = o->box.y1, x2 = o->box.x2, y2 = o->box.y2;
			if (x2 < x1) { double t = x1; x1 = x2; x2 = t; }
			if (y2 < y1) { double t = y1; y1 = y2; y2 = t; }
			double bw = x2 - x1, bh = y2 - y1;
			if (bw <= 0 || bh <= 0) continue;

			int cid = o->class_id;
			if (ncls > 0 && cid >= ncls) cid = ncls - 1;   /* 越界保护 */

			vif_det_t *d = &pkt.det[cnt];
			d->dx = c16((x1 + bw / 2) - W / 2);
			d->dy = c16(H / 2 - (y1 + bh / 2));
			d->x = c16(x1); d->y = c16(y1); d->w = c16(bw); d->h = c16(bh);
			d->area = u16c(bw * bh);
			d->cls = (uint8_t)cid;
			d->conf = conf_norm(o->score);
			conf_sum += d->conf; conf_n++;
			if (d->dx < dxmin) dxmin = d->dx;
			if (d->dx > dxmax) dxmax = d->dx;
			if (d->dy < dymin) dymin = d->dy;
			if (d->dy > dymax) dymax = d->dy;
			if ((double)d->area > best) { best = d->area; pkt.primary = (int16_t)cnt; }
			cnt++;
			det_total++;
		}
		pkt.n = (uint16_t)cnt;
		sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&dst, sizeof(dst));
		if (rec) fwrite(&pkt, sizeof(pkt), 1, rec);   /* 录原始 152 字节，供离线回放 */

		/* 调试：把程序实际看到的一帧存成 NV12（Y+UV），用于验证翻转/朝向 */
		if (dump_path && frames == 20) {
			VIDEO_FRAME_INFO_S *vf = nullptr;
			try { if (TDL_WrapImage(img, &vf) != 0) vf = nullptr; } catch (...) { vf = nullptr; }
			if (vf) {
				uint32_t ys = vf->stVFrame.u32Stride[0], yl = vf->stVFrame.u32Length[0];
				uint32_t us = vf->stVFrame.u32Stride[1], ul = vf->stVFrame.u32Length[1];
				uint8_t *yp = (uint8_t *)CVI_SYS_Mmap(vf->stVFrame.u64PhyAddr[0], yl);
				uint8_t *up = (uint8_t *)CVI_SYS_Mmap(vf->stVFrame.u64PhyAddr[1], ul);
				if (yp && up) {
					CVI_SYS_IonFlushCache(vf->stVFrame.u64PhyAddr[0], yp, yl);
					CVI_SYS_IonFlushCache(vf->stVFrame.u64PhyAddr[1], up, ul);
					FILE *fp = fopen(dump_path, "wb");
					if (fp) {
						for (uint32_t y = 0; y < (uint32_t)H; y++) fwrite(yp + (size_t)y * ys, 1, W, fp);
						for (uint32_t y = 0; y < (uint32_t)H / 2; y++) fwrite(up + (size_t)y * us, 1, W, fp);
						fclose(fp);
						printf("[mirovision] dump -> %s (Y=%u UV=%u stride=%u/%u)\n", dump_path, yl, ul, ys, us);
					}
				}
				if (yp) CVI_SYS_Munmap(yp, yl);
				if (up) CVI_SYS_Munmap(up, ul);
			}
		}

		/* M2：可选 RTSP 叠加（画框 + 类别/置信度） */
		if (rtsp_on) {
			VIDEO_FRAME_INFO_S *vf = nullptr;
			try { if (TDL_WrapImage(img, &vf) != 0) vf = nullptr; } catch (...) { vf = nullptr; }
			if (vf && cnt > 0) {
				uint32_t ys = vf->stVFrame.u32Stride[0], yl = vf->stVFrame.u32Length[0];
				uint8_t *yp = (uint8_t *)CVI_SYS_Mmap(vf->stVFrame.u64PhyAddr[0], yl);
				if (yp) {
					CVI_SYS_IonFlushCache(vf->stVFrame.u64PhyAddr[0], yp, yl);
					for (int i = 0; i < cnt; i++) {
						vif_det_t *d = &pkt.det[i];
						int x1 = d->x, y1 = d->y, x2 = d->x + d->w, y2 = d->y + d->h;
						draw_rect(yp, ys, W, H, x1, y1, x2, y2, 235, 3);
						char buf[24];
						snprintf(buf, sizeof(buf), "%d.%02d", d->conf / 100, d->conf % 100);
						draw_text(yp, ys, W, H, x1 + 2, (y1 > 20 ? y1 - 18 : y1 + 4), buf, 235, 3);
					}
					CVI_SYS_Munmap(yp, yl);
				}
			}
			if (vf) SendFrameRTSP(vf, &rtsp_ctx);
		}
		if (ok) TDL_ReleaseObjectMeta(&obj);
		ReleaseCameraFrame(h, 0);
		TDL_DestroyImage(img);

		uint64_t now = (uint64_t)now_ms();
		if (stats_sec > 0 && now - t_report >= (uint64_t)stats_sec * 1000) {
			printf("[stat] %.2fs fps=%.1f det=%ld/%ld infer=%.1fms conf=%.0f dx=[%d,%d] dy=[%d,%d]\n",
			       (now - t0) / 1000.0, frames / ((now - t0) / 1000.0),
			       det_frames, frames, infer_ms_sum / frames,
			       conf_n ? conf_sum / conf_n : 0.0, dxmin, dxmax, dymin, dymax);
			t_report = now;
			fflush(stdout);
		}
		if (max_frames > 0 && frames >= max_frames) break;
	}

	if (rec) fclose(rec);
	close(sock);
	TDL_CloseModel(h, TDL_MODEL_YOLOV8);
	DestoryCamera(h);
	TDL_DestroyHandle(h);
	printf("[mirovision] bye: frames=%ld det_frames=%ld det_total=%ld\n", frames, det_frames, det_total);
	return 0;
}
