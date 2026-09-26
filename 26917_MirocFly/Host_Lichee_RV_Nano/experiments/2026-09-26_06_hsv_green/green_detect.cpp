/*
 * green_detect.cpp - 实验06: 相机取帧 + HSV 绿色检测 (opencv-mobile) + BMP 可视化
 *
 * 数据流:
 *   VI pop NV21 -> cv::Mat(COLOR_YUV2BGR_NV21) -> resize(640x360) -> BGR2HSV
 *     -> inRange -> 形态学 -> findContours -> 最大轮廓 -> 质心/面积/偏移
 *   -> (每 N 帧) 缩略图画框 -> 直接写 green.bmp (不走硬件编码)
 *
 * 用法: ./green_detect [width] [height] [frames] [hmin hmax smin smax vmin vmax] [min_area]
 *   OpenCV HSV: H 0-179, S/V 0-255。默认对应 PIL H85-130/S40-170/V55-170。
 *   小目标可调大分辨率或调小 min_area。
 */
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <csignal>
#include <vector>
#include <unistd.h>

extern "C" {
#include "camera.h"
}

static volatile int g_exit = 0;
static void on_sig(int s) { (void)s; g_exit = 1; }

static const int DET_W = 640;
static const int DET_H = 360;

static void save_bmp_bgr(const char *path, const cv::Mat &bgr)
{
	int w = bgr.cols, h = bgr.rows;
	int row = ((w * 3 + 3) / 4) * 4;
	int datasize = row * h;
	int filesize = 54 + datasize;
	unsigned char hdr[54] = {0};
	hdr[0] = 'B'; hdr[1] = 'M';
	auto put32 = [&](int off, unsigned v) {
		hdr[off] = v & 0xff; hdr[off + 1] = (v >> 8) & 0xff;
		hdr[off + 2] = (v >> 16) & 0xff; hdr[off + 3] = (v >> 24) & 0xff;
	};
	put32(2, filesize); put32(10, 54); put32(14, 40);
	put32(18, w); put32(22, h);
	hdr[26] = 1; hdr[28] = 24; put32(34, datasize);
	FILE *fp = fopen(path, "wb");
	if (!fp)
		return;
	fwrite(hdr, 1, 54, fp);
	std::vector<unsigned char> line(row, 0);
	for (int j = h - 1; j >= 0; j--) {
		const unsigned char *src = bgr.ptr<unsigned char>(j);
		for (int i = 0; i < w * 3; i++)
			line[i] = src[i];
		fwrite(line.data(), 1, row, fp);
	}
	fclose(fp);
}

int main(int argc, char *argv[])
{
	int req_w = 1280, req_h = 720, frames = 0; /* frames=0 => 一直跑 */
	int hmin = 60, hmax = 91, smin = 40, smax = 170, vmin = 55, vmax = 170;
	int min_area = 20; /* 缩略图像素; 小目标调小 */
	int save_every = 5;

	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	if (argc > 1) req_w = atoi(argv[1]);
	if (argc > 2) req_h = atoi(argv[2]);
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 9) {
		hmin = atoi(argv[4]); hmax = atoi(argv[5]);
		smin = atoi(argv[6]); smax = atoi(argv[7]);
		vmin = atoi(argv[8]); vmax = atoi(argv[9]);
	}
	if (argc > 10) min_area = atoi(argv[10]);

	printf("=== MirocFly exp06 green_detect ===\n");
	printf("cam %dx%d frames=%d HSV(H %d-%d S %d-%d V %d-%d) min_area=%d\n",
	       req_w, req_h, frames, hmin, hmax, smin, smax, vmin, vmax, min_area);

	mf_camera_cfg_t cfg;
	mf_camera_cfg_default(&cfg);
	cfg.width = req_w;
	cfg.height = req_h;
	if (mf_camera_open(&cfg) != 0) {
		fprintf(stderr, "mf_camera_open failed\n");
		return -1;
	}
	printf("sensor id 0x%x\n", mf_camera_sensor_id());
	mf_camera_warmup(10);
	printf("warmup done\n");

	int seq = 0, saved = 0, found_cnt = 0;
	while (!g_exit) {
		mf_frame_t f;
		if (mf_camera_get_frame(&f, 5000) != 0) {
			fprintf(stderr, "frame timeout\n");
			break;
		}
		int w = f.width, h = f.height;
		cv::Mat yuv(h * 3 / 2, w, CV_8UC1, (void *)f.data);
		cv::Mat bgr;
		cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV21);
		mf_camera_frame_free();

		cv::Mat small;
		cv::resize(bgr, small, cv::Size(DET_W, DET_H));

		cv::Mat hsv, mask;
		cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
		cv::inRange(hsv, cv::Scalar(hmin, smin, vmin), cv::Scalar(hmax, smax, vmax), mask);
		cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
		cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
		cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

		std::vector<std::vector<cv::Point>> contours;
		cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

		bool found = false;
		cv::Point2f centroid(0, 0);
		double area_small = 0;
		int best = -1;
		for (size_t i = 0; i < contours.size(); i++) {
			double a = cv::contourArea(contours[i]);
			if (a >= min_area && a > area_small) {
				area_small = a;
				best = (int)i;
			}
		}
		if (best >= 0) {
			cv::Moments m = cv::moments(contours[best]);
			if (m.m00 > 0) {
				centroid = cv::Point2f((float)(m.m10 / m.m00), (float)(m.m01 / m.m00));
				found = true;
			}
		}

		if (found) {
			double sx = (double)w / DET_W, sy = (double)h / DET_H;
			int offx = (int)((centroid.x - DET_W / 2.0) * sx);
			int offy = (int)((centroid.y - DET_H / 2.0) * sy);
			double area = area_small * sx * sy;
			found_cnt++;
			printf("seq=%d FOUND offset=(%d,%d) area=%.0f centroid_small=(%.0f,%.0f)\n",
			       seq, offx, offy, area, centroid.x, centroid.y);
			cv::rectangle(small, cv::boundingRect(contours[best]), cv::Scalar(0, 0, 255), 2);
			cv::circle(small, centroid, 4, cv::Scalar(0, 255, 255), -1);
		} else if (seq % 30 == 0) {
			printf("seq=%d no green\n", seq);
		}
		cv::line(small, cv::Point(DET_W / 2, DET_H / 2 - 20), cv::Point(DET_W / 2, DET_H / 2 + 20), cv::Scalar(255, 0, 0), 2);
		cv::line(small, cv::Point(DET_W / 2 - 20, DET_H / 2), cv::Point(DET_W / 2 + 20, DET_H / 2), cv::Scalar(255, 0, 0), 2);

		if (seq % save_every == 0) {
			save_bmp_bgr("green.bmp", small);
			saved++;
		}

		seq++;
		if (frames > 0 && seq >= frames)
			break;
	}

	mf_camera_close();
	printf("done: frames=%d found=%d bmp_saved=%d\n", seq, found_cnt, saved);
	return 0;
}
