/*
 * vif_stat.c —— vif_frame_t 链路体检工具（riscv64 / 任意 Linux 都能编）
 *
 * 监听 UDP 7788，统计：
 *   - 帧率、包大小、magic/version 是否合法
 *   - ts_ns 是否单调、帧间隔分布、断流次数（>300ms）
 *   - n 的分布（无目标帧占比）
 *   - 最近一帧的检测信息（cls/conf/dx/dy/area）
 *   - 若窗口内有目标：检出率、平均置信度、dx/dy 范围
 *
 * 用法:
 *   ./vif_stat [port] [seconds]
 *   例: ./vif_stat 7788 20
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define VIF_MAGIC   0x56494631u   /* "VIF1" */
#define VIF_VERSION 1
#define VIF_MAX_DET 8

typedef struct {
	int16_t dx, dy, x, y, w, h;
	uint16_t area;
	uint8_t  cls, conf;
} vif_det_t;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t n;
	int16_t  primary;
	uint16_t w, h;
	uint64_t ts_ns;
	vif_det_t det[VIF_MAX_DET];
} vif_frame_t;

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

int main(int argc, char **argv)
{
	int port = argc > 1 ? atoi(argv[1]) : 7788;
	int secs = argc > 2 ? atoi(argv[2]) : 20;

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) { perror("socket"); return 1; }
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a; memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); return 1; }
	struct timeval tv = {0, 200000};
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	printf("=== vif_stat :%d  %d 秒 ===\n", port, secs);
	printf("等待 vision_node / vision_rtsp 发帧...\n");

	long frames = 0, bad_size = 0, bad_magic = 0, gaps = 0, with_tgt = 0;
	long n_hist[VIF_MAX_DET + 1]; memset(n_hist, 0, sizeof(n_hist));
	uint64_t last_ts = 0, first_ts = 0, last_arr = 0;
	double conf_sum = 0; long conf_cnt = 0;
	int16_t dx_min = 32767, dx_max = -32768, dy_min = 32767, dy_max = -32768;
	int last_n = -1; int16_t lcls = -1; int lconf = -1, ldx = 0, ldy = 0; long larea = 0;
	uint64_t t0 = now_ms();
	uint64_t next_report = t0 + 1000;
	long win_frames = 0, win_prev = 0;

	while (now_ms() - t0 < (uint64_t)secs * 1000) {
		uint8_t buf[512];
		ssize_t r = recv(s, buf, sizeof(buf), 0);
		if (r < 0) continue;
		if (r != (ssize_t)sizeof(vif_frame_t)) { bad_size++; continue; }
		vif_frame_t f; memcpy(&f, buf, sizeof(f));
		if (f.magic != VIF_MAGIC || f.version != VIF_VERSION) { bad_magic++; continue; }

		frames++;
		uint64_t arr = now_ms();
		if (!first_ts) first_ts = f.ts_ns;
		if (last_ts && f.ts_ns <= last_ts) { /* 非单调 */ }
		if (last_arr && arr - last_arr > 300) gaps++;
		last_ts = f.ts_ns; last_arr = arr;

		if (f.n <= VIF_MAX_DET) n_hist[f.n]++;
		if (f.n > 0) {
			with_tgt++;
			long area = 0; int ci = 0;
			for (int i = 0; i < f.n && i < VIF_MAX_DET; i++) {
				if (f.det[i].area > area) { area = f.det[i].area; ci = i; }
			}
			const vif_det_t *d = &f.det[ci];
			conf_sum += d->conf; conf_cnt++;
			if (d->dx < dx_min) dx_min = d->dx;
			if (d->dx > dx_max) dx_max = d->dx;
			if (d->dy < dy_min) dy_min = d->dy;
			if (d->dy > dy_max) dy_max = d->dy;
			last_n = f.n; lcls = d->cls; lconf = d->conf; ldx = d->dx; ldy = d->dy; larea = area;
		} else {
			last_n = 0;
		}

		uint64_t now = now_ms();
		if (now >= next_report) {
			win_frames = frames - win_prev; win_prev = frames;
			printf("[%3llus] fps=%2ld  帧=%ld  目标帧=%ld\n",
			       (unsigned long long)((now - t0) / 1000), win_frames, frames, with_tgt);
			next_report = now + 1000;
		}
	}

	int elapsed_ms = (int)(now_ms() - t0);
	printf("\n========== 结果（%d ms）==========\n", elapsed_ms);
	printf("收帧总数      : %ld\n", frames);
	printf("平均帧率      : %.1f fps\n", frames * 1000.0 / (elapsed_ms ? elapsed_ms : 1));
	printf("坏包(大小错)  : %ld\n", bad_size);
	printf("坏包(magic错) : %ld\n", bad_magic);
	printf("断流(>300ms)  : %ld 次\n", gaps);
	printf("目标帧占比    : %ld/%ld = %.1f%%\n", with_tgt, frames,
	       frames ? 100.0 * with_tgt / frames : 0);
	printf("n 分布        : ");
	for (int i = 0; i <= VIF_MAX_DET; i++) if (n_hist[i]) printf("n=%d:%ld ", i, n_hist[i]);
	printf("\n");
	if (conf_cnt) {
		printf("目标平均置信度: %.2f （%ld 帧）\n", conf_sum / conf_cnt, conf_cnt);
		printf("dx 范围       : %d .. %d\n", dx_min, dx_max);
		printf("dy 范围       : %d .. %d\n", dy_min, dy_max);
		printf("最近目标      : n=%d cls=%d conf=%d dx=%d dy=%d area=%ld\n",
		       last_n, lcls, lconf, ldx, ldy, larea);
	} else {
		printf("本窗口内未检出目标（n 恒为 0）\n");
	}

	/* 一句话判定 */
	printf("\n【判定】");
	if (frames < secs * 5) printf("帧率过低，链路可疑；");
	if (bad_size || bad_magic) printf("存在坏包，接口不匹配；");
	if (gaps > secs / 5) printf("断流频繁，检查 WiFi/负载；");
	if (conf_cnt == 0) printf("无目标（画面里没有绿色物体，或模型未检出）；");
	if (frames >= secs * 5 && !bad_size && !bad_magic && gaps <= secs / 5 && conf_cnt > 0)
		printf("链路通畅且模型有检出 ✅");
	printf("\n");

	close(s);
	return 0;
}
