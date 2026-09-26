/*
 * camera.c - 上位机相机抓帧 (scpcom 中间件 Form A)
 *
 * 用 scpcom 源码树的 sophgo_middleware.c + 板上 scpcom 库。
 * API: mmf_init/mmf_vi_init/mmf_add_vi_channel/mmf_vi_frame_pop
 *
 * 用法: ./camera [width] [height] [frames] [warmup] [keep] [hmirror] [vflip]
 *   输出 cam_<w>x<h>[_n].raw (NV21)
 *   warmup: 丢弃的前导帧数(默认10, 流水线启动帧是黑的)
 *   keep  : cam_*.{raw,bmp,png} 最多保留数量(默认10, 0=不清理)
 *           也支持环境变量 MIROCFLY_KEEP
 *   hmirror/vflip: 1=启用该方向翻转(默认0)。中间件默认给 VPSS 传
 *           bMirror=true,bFlip=true(等于旋转180°), 故常见安装需 hmirror=1 vflip=1 摆正。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

#include "sophgo_middleware.h"
#include "cvi_buffer.h" /* PIXEL_FORMAT_NV21 等 */

static volatile int g_exit = 0;
static void sig_handler(int s) { (void)s; g_exit = 1; }

struct prune_ent { char name[256]; time_t mt; };

static int prune_name_ok(const char *n)
{
	if (strncmp(n, "cam_", 4) != 0)
		return 0;
	const char *dot = strrchr(n, '.');
	if (!dot)
		return 0;
	return !strcmp(dot, ".raw") || !strcmp(dot, ".bmp") || !strcmp(dot, ".png");
}

static int prune_cmp_mt(const void *a, const void *b)
{
	time_t ta = ((const struct prune_ent *)a)->mt;
	time_t tb = ((const struct prune_ent *)b)->mt;
	return (ta < tb) ? -1 : (ta > tb) ? 1 : 0;
}

/* 保留最新的 keep 个 cam_* 输出, 其余(旧的)删除, 限制磁盘占用 */
static void prune_outputs(int keep)
{
	if (keep <= 0)
		return;
	DIR *d = opendir(".");
	if (!d)
		return;
	struct prune_ent *v = NULL;
	int n = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (!prune_name_ok(e->d_name))
			continue;
		struct stat st;
		if (stat(e->d_name, &st) != 0)
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 16;
			v = realloc(v, (size_t)cap * sizeof(*v));
		}
		snprintf(v[n].name, sizeof(v[n].name), "%s", e->d_name);
		v[n].mt = st.st_mtime;
		n++;
	}
	closedir(d);
	if (n > keep) {
		qsort(v, (size_t)n, sizeof(*v), prune_cmp_mt);
		for (int i = 0; i < n - keep; i++) {
			if (unlink(v[i].name) == 0)
				printf("pruned old %s\n", v[i].name);
		}
	}
	free(v);
}

int main(int argc, char *argv[])
{
	int req_w = 1280, req_h = 720;
	int frames = 1;
	int warmup = 10;
	int keep = 10;
	int hmirror = 0, vflip = 0;
	const char *env_keep = getenv("MIROCFLY_KEEP");

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	if (argc > 1) req_w = atoi(argv[1]);
	if (argc > 2) req_h = atoi(argv[2]);
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 4) warmup = atoi(argv[4]);
	if (argc > 5) keep = atoi(argv[5]);
	else if (env_keep) keep = atoi(env_keep);
	if (argc > 6) hmirror = atoi(argv[6]);
	if (argc > 7) vflip = atoi(argv[7]);
	if (frames < 1) frames = 1;
	if (warmup < 0) warmup = 0;

	printf("=== MirocFly camera (scpcom Form A, NV21) ===\n");
	printf("request: %dx%d frames=%d warmup=%d keep=%d hmirror=%d vflip=%d\n",
	       req_w, req_h, frames, warmup, keep, hmirror, vflip);

	prune_outputs(keep);

	if (mmf_init() != 0) { fprintf(stderr, "mmf_init failed\n"); return -1; }
	printf("sensor id: 0x%x\n", mmf_get_sensor_id());

	if (mmf_vi_init() != 0) { fprintf(stderr, "mmf_vi_init failed\n"); mmf_deinit(); return -1; }

	int ch = mmf_get_vi_unused_channel();
	printf("vi ch = %d\n", ch);
	if (hmirror) mmf_set_vi_hmirror(ch, true);
	if (vflip) mmf_set_vi_vflip(ch, true);
	if (mmf_add_vi_channel(ch, req_w, req_h, PIXEL_FORMAT_NV21) != 0) {
		fprintf(stderr, "mmf_add_vi_channel(%dx%d) failed\n", req_w, req_h);
		mmf_vi_deinit(); mmf_deinit(); return -1;
	}
	printf("vi channel added\n");

	void *data; int len = 0, w = 0, h = 0, fmt = 0;

	/* 预热: 丢弃前导黑帧/未稳定帧 */
	int warm = 0, wait = 0;
	while (warm < warmup && !g_exit) {
		if (mmf_vi_frame_pop(ch, &data, &len, &w, &h, &fmt) == 0) {
			mmf_vi_frame_free(ch);
			warm++;
		} else {
			if (++wait > 100) { fprintf(stderr, "warmup timeout\n"); break; }
			usleep(20 * 1000);
		}
	}
	printf("warmup done: %d frames\n", warm);

	int saved = 0;
	for (int f = 0; f < frames && !g_exit; f++) {
		int tries = 0;
		while (mmf_vi_frame_pop(ch, &data, &len, &w, &h, &fmt) != 0) {
			if (++tries > 100) { fprintf(stderr, "frame pop timeout\n"); goto out; }
			usleep(20 * 1000);
		}
		char name[128];
		if (frames == 1)
			snprintf(name, sizeof(name), "cam_%dx%d.raw", w, h);
		else
			snprintf(name, sizeof(name), "cam_%dx%d_%d.raw", w, h, f);
		FILE *fp = fopen(name, "wb");
		if (fp) {
			fwrite(data, 1, (size_t)len, fp);
			fclose(fp);
			printf("saved %s (%d bytes, %dx%d fmt=%d)\n", name, len, w, h, fmt);
			saved++;
		}
		mmf_vi_frame_free(ch);
	}
	prune_outputs(keep);

out:
	mmf_del_vi_channel(ch);
	mmf_vi_deinit();
	mmf_deinit();
	printf("done (%d frames saved)\n", saved);
	return 0;
}
