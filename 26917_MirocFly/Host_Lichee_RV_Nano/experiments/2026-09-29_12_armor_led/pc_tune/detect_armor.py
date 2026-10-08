#!/usr/bin/env python3
"""实验12 S1: 传统 CV 装甲板检测原型 (OpenCV 5, 与板端 opencv-mobile 5.0.0 对齐)。

思路(对应 S0 讲过的要点):
  1) 通道差分割: 红 = R - max(G,B); 蓝 = B - max(R,G)   (比 HSV 抗亮度变化)
  2) 形态学开/闭去噪
  3) findContours + minAreaRect -> 灯条(long bar), 按 长宽比/长度/面积 过滤
  4) 同色"松弛配对": 长度比/角度差/y差/间距比 给容差 (允许透视梯形、不等长)
  5) 装甲板中心 = 四点四边形对角线交点 (对梯形稳健)

用法:
  python3 detect_armor.py                 # 默认跑 ../synth_armor, 对照 labels.csv 评估
  python3 detect_armor.py --indir ../synth_armor --out out
  python3 detect_armor.py --img ../synth_armor/red_yaw_strong.png   # 单图
可调: --red-thr --blue-thr --min-area --min-len --aspect --len-ratio
      --ang-diff --ydiff --gap --debug
"""
import os
import csv
import glob
import math
import argparse

import cv2
import numpy as np


# ---------------- 灯条表示 ----------------
class Bar:
    __slots__ = ('color', 'c', 'p1', 'p2', 'length', 'width', 'ang')

    def __init__(self, color, center, box):
        # box: 4x2 灯条四角。中轴 = 两条"短边"的中点连线(灯条中心线),
        # 这样不受 boxPoints 角点顺序(内/外边)影响, 中心无偏。
        pts = [tuple(map(float, p)) for p in box]
        edges = [(0, 1), (1, 2), (2, 3), (3, 0)]
        elen = [math.dist(pts[i], pts[j]) for i, j in edges]
        sh = sorted(range(4), key=lambda k: elen[k])[:2]
        mids = []
        for k in sh:
            i, j = edges[k]
            mids.append(((pts[i][0] + pts[j][0]) / 2.0,
                         (pts[i][1] + pts[j][1]) / 2.0))
        self.p1, self.p2 = mids[0], mids[1]
        self.length = math.dist(self.p1, self.p2)
        self.width = (elen[sh[0]] + elen[sh[1]]) / 2.0
        self.c = center
        ang = math.degrees(math.atan2(self.p2[1] - self.p1[1],
                                      self.p2[0] - self.p1[0])) % 180.0
        self.ang = ang
        self.color = color

    def tips(self):
        """返回 (top, bottom) —— 按 y 排序的两个长轴端点。"""
        return (self.p1, self.p2) if self.p1[1] <= self.p2[1] else (self.p2, self.p1)


def channel_diff_mask(img, color, thr):
    """通道差 -> 二值掩码。img 为 BGR uint8。"""
    b = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    r = img[:, :, 2].astype(np.int16)
    if color == 'red':
        diff = r - np.maximum(g, b)
    else:
        diff = b - np.maximum(r, g)
    diff = np.clip(diff, 0, 255).astype(np.uint8)
    return cv2.threshold(diff, thr, 255, cv2.THRESH_BINARY)[1]


def find_bars(mask, color, cfg, img):
    k3 = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    k5 = cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5))
    m = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k3)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, k5)
    contours = cv2.findContours(m, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)[0]
    # 颜色纯度门: LED 的高纯色 => 另外两个通道很低(如红 LED 的 G/B≈70-90);
    # 皮肤/背景的红往往 G/B 高(~140) -> 用均值门剔除, 从而可用较低阈值抓暗 LED。
    other = (0, 1) if color == 'red' else (2, 1)
    chan = [img[:, :, c] for c in other]
    bars = []
    for ct in contours:
        if cv2.contourArea(ct) < cfg.min_area:
            continue
        if cfg.max_gb is not None and cfg.max_gb >= 0:
            full = np.zeros(m.shape, np.uint8)
            cv2.drawContours(full, [ct], -1, 255, -1)
            sel = full > 0
            if sel.sum() == 0:
                continue
            if any(float(ch[sel].mean()) > cfg.max_gb for ch in chan):
                continue
        rect = cv2.minAreaRect(ct)
        box = cv2.boxPoints(rect)
        center = (float(rect[0][0]), float(rect[0][1]))
        bar = Bar(color, center, box)
        if bar.length < cfg.min_len:
            continue
        aspect = bar.length / max(bar.width, 1e-6)
        if not (cfg.aspect[0] <= aspect <= cfg.aspect[1]):
            continue
        bars.append(bar)
    return bars


def pair_bars(bars, cfg):
    """同色松弛配对 -> 装甲板列表 [{'color','center','bars','quad','score'}]"""
    cands = []
    n = len(bars)
    for i in range(n):
        for j in range(i + 1, n):
            a, b = bars[i], bars[j]
            if a.color != b.color:
                continue
            lmax, lmin = max(a.length, b.length), max(1e-6, min(a.length, b.length))
            len_ratio = lmax / lmin
            if len_ratio > cfg.len_ratio:
                continue
            dang = abs(a.ang - b.ang)
            dang = min(dang, 180.0 - dang)
            if dang > cfg.ang_diff:
                continue
            mean_len = (a.length + b.length) / 2.0
            dy = abs(a.c[1] - b.c[1]) / mean_len
            if dy > cfg.ydiff:
                continue
            gap = abs(a.c[0] - b.c[0]) / mean_len
            if not (cfg.gap[0] <= gap <= cfg.gap[1]):
                continue
            # 两灯条应左右并排, 不应几乎重合
            if abs(a.c[0] - b.c[0]) < 0.5 * (a.width + b.width):
                continue
            score = (len_ratio - 1.0) + dang / cfg.ang_diff + dy / cfg.ydiff
            cands.append((score, i, j, a, b))
    cands.sort(key=lambda t: t[0])
    used, armors = set(), []
    for score, i, j, a, b in cands:
        if i in used or j in used:
            continue
        used.update((i, j))
        left, right = (a, b) if a.c[0] <= b.c[0] else (b, a)
        lt, lb = left.tips()
        rt, rb = right.tips()
        quad = [lt, lb, rb, rt]            # TL, BL, BR, TR
        center = diag_intersect(lt, rb, lb, rt)
        armors.append({'color': a.color, 'center': center,
                       'bars': (left, right), 'quad': quad, 'score': score})
    return armors


def diag_intersect(p1, p2, p3, p4):
    x1, y1 = p1; x2, y2 = p2; x3, y3 = p3; x4, y4 = p4
    den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
    if abs(den) < 1e-9:
        return ((x1 + x3) / 2.0, (y1 + y3) / 2.0)
    px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / den
    py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / den
    return (px, py)


COL = {'red': (0, 0, 255), 'blue': (255, 0, 0)}


def detect(img, cfg):
    bars = []
    for color in ('red', 'blue'):
        thr = cfg.red_thr if color == 'red' else cfg.blue_thr
        mask = channel_diff_mask(img, color, thr)
        if cfg.debug:
            cv2.imwrite(f'out/mask_{color}.png', mask)
        bars += find_bars(mask, color, cfg, img)
    return bars, pair_bars(bars, cfg)


def draw(img, bars, armors):
    out = img.copy()
    for b in bars:
        cv2.polylines(out, [np.array([b.p1, b.p2], dtype=np.int32)], False,
                      COL[b.color], 2)
        cv2.circle(out, tuple(map(int, b.c)), 3, (0, 255, 255), -1)
    for a in armors:
        q = np.array([a['quad']], dtype=np.int32)
        cv2.polylines(out, q, True, (0, 255, 0), 2)
        c = tuple(map(int, a['center']))
        cv2.circle(out, c, 6, (0, 255, 0), -1)
        cv2.putText(out, f"{a['color']} {math.hypot(a['center'][0]-out.shape[1]/2, a['center'][1]-out.shape[0]/2):.0f}px",
                    (c[0] + 8, c[1] - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
    # 画面中心十字
    h, w = out.shape[:2]
    cv2.drawMarker(out, (w // 2, h // 2), (0, 255, 255), cv2.MARKER_CROSS, 30, 2)
    return out


def load_labels(path):
    gt = {}
    if not path or not os.path.exists(path):
        return gt
    with open(path) as f:
        for row in csv.DictReader(f):
            gt.setdefault(row['name'], []).append((row['color'],
                                                   float(row['cx']), float(row['cy'])))
    return gt


def main():
    base = os.path.normpath(os.path.join(os.path.dirname(__file__), '..'))
    ap = argparse.ArgumentParser()
    ap.add_argument('--indir', default=os.path.join(base, 'synth_armor'))
    ap.add_argument('--labels', default=os.path.join(base, 'synth_armor', 'labels.csv'))
    ap.add_argument('--out', default=os.path.join(base, 'synth_armor', 'out'))
    ap.add_argument('--img', default=None)
    ap.add_argument('--red-thr', type=int, default=75)
    ap.add_argument('--blue-thr', type=int, default=75)
    ap.add_argument('--min-area', type=float, default=25)
    ap.add_argument('--min-len', type=float, default=8)
    ap.add_argument('--aspect', nargs=2, type=float, default=[1.3, 14.0])
    ap.add_argument('--max-gb', type=float, default=120,
                    help='颜色纯度门: 非主色两通道均值上限(剔除皮肤/背景); <0 关闭')
    ap.add_argument('--len-ratio', type=float, default=2.2)
    ap.add_argument('--ang-diff', type=float, default=20)
    ap.add_argument('--ydiff', type=float, default=0.6)
    ap.add_argument('--gap', nargs=2, type=float, default=[0.5, 4.0])
    ap.add_argument('--match-px', type=float, default=40)
    ap.add_argument('--debug', action='store_true')
    cfg = ap.parse_args()
    os.makedirs(cfg.out, exist_ok=True)

    if cfg.img:
        files = [cfg.img]
        gt = {}
    else:
        files = sorted(glob.glob(os.path.join(cfg.indir, '*.png')))
        files = [f for f in files if 'contact_sheet' not in f]
        gt = load_labels(cfg.labels)

    tot_gt = tot_det = tot_hit = 0
    errs = []
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        img = cv2.imread(f)
        if img is None:
            continue
        bars, armors = detect(img, cfg)
        cv2.imwrite(os.path.join(cfg.out, name + '.png'), draw(img, bars, armors))
        truths = gt.get(name, [])
        dets = [(a['color'], a['center']) for a in armors]
        h, w = img.shape[:2]
        detdesc = '  '.join(
            f"{c}@({x:.0f},{y:.0f}) off=({x - w / 2:+.0f},{y - h / 2:+.0f})"
            for c, (x, y) in dets)
        used = [False] * len(dets)
        line, hits = [], 0
        for (gcol, gx, gy) in truths:
            best, bd = -1, cfg.match_px
            for k, (dcol, dc) in enumerate(dets):
                if used[k] or dcol != gcol:
                    continue
                d = math.hypot(dc[0] - gx, dc[1] - gy)
                if d < bd:
                    bd, best = d, k
            if best >= 0:
                used[best] = True
                hits += 1
                errs.append(bd)
                line.append(f'{gcol} err={bd:.1f}px')
            else:
                line.append(f'{gcol} MISS')
        tot_gt += len(truths)
        tot_det += len(dets)
        tot_hit += hits
        print(f'{name:24s} bars={len(bars):2d} armor={len(dets)}  {detdesc}  ' + '; '.join(line))

    print('\n===== 汇总 =====')
    recall = tot_hit / tot_gt if tot_gt else float('nan')
    prec = tot_hit / tot_det if tot_det else float('nan')
    print(f'真值 {tot_gt}  检出 {tot_det}  命中 {tot_hit}  '
          f'召回={recall:.2%}  精确={prec:.2%}')
    if errs:
        print(f'中心误差: 平均 {np.mean(errs):.2f}px  中位 {np.median(errs):.2f}px  '
              f'最大 {max(errs):.2f}px')


if __name__ == '__main__':
    main()
