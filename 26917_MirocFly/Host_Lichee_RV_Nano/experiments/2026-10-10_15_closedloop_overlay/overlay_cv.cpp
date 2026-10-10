/*
 * overlay_cv.cpp - 实验15: opencv 侧 (NV21->BGR 绿检 + 偏移EMA平滑/丢检滞回 + Y平面叠加)
 * 由 loop_overlay.c 每帧调用 om_process(); 不 include maix_mmf.h (避免 C/C++ 链接混用)。
 */
#include <cstdio>
#include <cstring>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry.hpp>

#include "overlay_api.h"

static const int DET_W = 640, DET_H = 360;

extern "C" int om_process(uint8_t *nv21, int w, int h, const om_state_t *st, om_ctx_t *ctx)
{
	cv::Mat yuv(h * 3 / 2, w, CV_8UC1, nv21);
	cv::Mat bgr;
	cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV21);

	cv::Mat small, hsv, mask;
	cv::resize(bgr, small, cv::Size(DET_W, DET_H));
	cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
	cv::inRange(hsv, cv::Scalar(ctx->hmin, ctx->smin, ctx->vmin),
	            cv::Scalar(ctx->hmax, ctx->smax, ctx->vmax), mask);
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
	cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
	cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
	int best = -1; double best_a = 0;
	for (size_t i = 0; i < contours.size(); i++) {
		double a = cv::contourArea(contours[i]);
		if (a >= ctx->min_area && a > best_a) { best_a = a; best = (int)i; }
	}

	ctx->found = 0; ctx->offx = 0; ctx->offy = 0;
	ctx->bx1 = ctx->by1 = ctx->bx2 = ctx->by2 = 0;
	if (best >= 0) {
		cv::Moments m = cv::moments(contours[best]);
		cv::Rect r = cv::boundingRect(contours[best]);
		if (m.m00 > 0) {
			double kx = (double)w / DET_W, ky = (double)h / DET_H;
			double cx = m.m10 / m.m00, cy = m.m01 / m.m00;
			ctx->offx = (int)((cx - DET_W / 2.0) * kx);
			ctx->offy = (int)((cy - DET_H / 2.0) * ky);
			ctx->bx1 = (int)(r.x * kx); ctx->by1 = (int)(r.y * ky);
			ctx->bx2 = (int)((r.x + r.width) * kx); ctx->by2 = (int)((r.y + r.height) * ky);
			ctx->found = 1;
		}
	}

	/* EMA 平滑 + 丢检滞回 */
	int send_target = 0;
	if (ctx->found) {
		ctx->lost = 0;
		if (!ctx->tracking) { ctx->sx = ctx->offx; ctx->sy = ctx->offy; ctx->tracking = 1; }
		else { ctx->sx = ctx->alpha * ctx->offx + (1 - ctx->alpha) * ctx->sx;
		       ctx->sy = ctx->alpha * ctx->offy + (1 - ctx->alpha) * ctx->sy; }
		send_target = 1;
	} else if (ctx->tracking) {
		if (++ctx->lost > ctx->hold) ctx->tracking = 0;
		else { ctx->sx *= ctx->hold_decay; ctx->sy *= ctx->hold_decay; send_target = 1; }
	}
	ctx->sent = send_target;

	/* ---- 在本帧 Y 平面叠加 (单通道写亮度) ---- */
	cv::Mat y(h, w, CV_8UC1, nv21);
	cv::drawMarker(y, cv::Point(w / 2, h / 2), cv::Scalar(180), cv::MARKER_CROSS, 26, 1);
	if (ctx->found)
		cv::rectangle(y, cv::Point(ctx->bx1, ctx->by1), cv::Point(ctx->bx2, ctx->by2), cv::Scalar(255), 2);
	if (send_target) {
		int cx = w / 2 + (int)ctx->sx, cy = h / 2 + (int)ctx->sy;
		cv::drawMarker(y, cv::Point(cx, cy), cv::Scalar(255), cv::MARKER_CROSS, 18, 2);
		cv::line(y, cv::Point(w / 2, h / 2), cv::Point(cx, cy), cv::Scalar(255), 1);
	}
	char l0[160], l1[160], l2[160];
	const char *tag = ctx->found ? "TRACK" : (send_target ? "HOLD" : "LOST");
	snprintf(l0, sizeof l0, "%s off=(%d,%d)", tag, (int)ctx->sx, (int)ctx->sy);
	snprintf(l1, sizeof l1, "RC roll=%d pitch=%d thr=%d", st->rc_roll, st->rc_pitch, st->rc_thr);
	snprintf(l2, sizeof l2, "FC r=%.1f p=%.1f y=%.1f acc=%d,%d,%d%s",
	         st->fc_roll / 10.0, st->fc_pitch / 10.0, st->fc_yaw / 10.0,
	         st->fc_ax, st->fc_ay, st->fc_az, st->tel_ok ? "" : " (no tel)");
	cv::putText(y, l0, cv::Point(12, 34), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(255), 2);
	cv::putText(y, l1, cv::Point(12, 66), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(255), 2);
	cv::putText(y, l2, cv::Point(12, 98), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(255), 2);

	return send_target;
}
