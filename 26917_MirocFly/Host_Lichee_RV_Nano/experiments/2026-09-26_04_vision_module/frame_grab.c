/*
 * frame_grab.c - 相机层 CLI demo: 抓 N 帧存 NV21 raw。
 *
 * 用法: ./frame_grab [width] [height] [frames] [warmup] [keep] [hmirror] [vflip]
 *   warmup : 抓帧前丢弃的前导帧数(默认10)  [见 difficulty_and_method.md B9]
 *   keep   : cam_*.{raw,bmp,png} 最多保留数(默认10, 0=不清理), 也可用 MIROCFLY_KEEP
 *   hmirror/vflip: 软件朝向开关(默认0)
 */
#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

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
	mf_camera_cfg_t cfg;
	mf_camera_cfg_default(&cfg);
	int frames = 1, warmup = 10, keep = 10;
	const char *env_keep = getenv("MIROCFLY_KEEP");

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	if (argc > 1) cfg.width = atoi(argv[1]);
	if (argc > 2) cfg.height = atoi(argv[2]);
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 4) warmup = atoi(argv[4]);
	if (argc > 5) keep = atoi(argv[5]);
	else if (env_keep) keep = atoi(env_keep);
	if (argc > 6) cfg.hmirror = atoi(argv[6]);
	if (argc > 7) cfg.vflip = atoi(argv[7]);
	if (frames < 1) frames = 1;
	if (warmup < 0) warmup = 0;

	printf("=== MirocFly frame_grab (vision/camera, Form A) ===\n");
	printf("request: %dx%d frames=%d warmup=%d keep=%d hmirror=%d vflip=%d\n",
	       cfg.width, cfg.height, frames, warmup, keep, cfg.hmirror, cfg.vflip);

	prune_outputs(keep);

	if (mf_camera_open(&cfg) != 0) {
		fprintf(stderr, "mf_camera_open failed\n");
		return -1;
	}
	printf("sensor id: 0x%x, vi ch opened\n", mf_camera_sensor_id());

	int warm = mf_camera_warmup(warmup);
	printf("warmup done: %d frames\n", warm);

	int saved = 0;
	for (int f = 0; f < frames && !g_exit; f++) {
		mf_frame_t frame;
		if (mf_camera_get_frame(&frame, 5000) != 0) {
			fprintf(stderr, "frame pop timeout at %d\n", f);
			break;
		}
		char name[128];
		if (frames == 1)
			snprintf(name, sizeof(name), "cam_%dx%d.raw", frame.width, frame.height);
		else
			snprintf(name, sizeof(name), "cam_%dx%d_%d.raw", frame.width, frame.height, f);
		FILE *fp = fopen(name, "wb");
		if (fp) {
			fwrite(frame.data, 1, (size_t)frame.size, fp);
			fclose(fp);
			printf("saved %s (%d bytes, %dx%d fmt=%d)\n",
			       name, frame.size, frame.width, frame.height, frame.format);
			saved++;
		}
		mf_camera_frame_free();
	}
	prune_outputs(keep);
	mf_camera_close();
	printf("done (%d frames saved)\n", saved);
	return 0;
}
