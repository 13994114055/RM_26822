#!/usr/bin/env python3
"""实验12 S0: 生成"透视(梯形)装甲板"合成测试图 —— 纯 PIL, 不需要 numpy / cv2。

装甲板结构(阶段2/RM 风格): 黑底 + 两侧同色长条 LED(红或蓝) + 可选白色数字。
本脚本只造"LED 灯条"版(当前手头只有灯条)。

为什么要透视而不是简单旋转:
  真实相机是透视投影, 装甲板有偏航(yaw)/俯仰(pitch)/横滚(roll)时:
    - 偏航: 近侧灯条长、远侧短 -> 左右不等长; 两灯条间距变小 (梯形)
    - 俯仰: 上下边缘不等宽, 灯条倾斜
    - 横滚: 两灯条一起倾斜(仍近似平行)
  所以必须用"单应变换(homography)"渲染成梯形, 才能覆盖"不等长/不对称"的真实情况。
  这也是 S1 检测算法必须用"松弛约束 + 四点四边形"而不是"等高对称"的原因。

用法:
  python3 gen_synth_armor.py [--out synth_armor] [--seed 0] [--canvas 1280 720]
输出:
  <out>/<name>.png          测试图
  <out>/labels.csv          真值: name,color,cx,cy,x0,y0,x1,y1,x2,y2,x3,y3,digit
                            (cx,cy = 板中心; x0..x3 = 装甲板四角 TL,BL,BR,TR)
  <out>/contact_sheet.png   缩略总览
"""
import os
import csv
import math
import random
import argparse

from PIL import Image, ImageDraw, ImageChops, ImageEnhance

# ------- 装甲板"标准件"模板参数 (源图坐标) -------
AW, AH = 300, 210          # 装甲板(黑底)尺寸
BAR_W, BAR_H = 20, 140     # LED 灯条宽/高
BAR_MARGIN = 30            # 灯条离板左右边缘
COLORS = {
    'red':  (255, 60, 60),
    'blue': (40, 90, 255),
}


def solve_h(from_quad, to_quad):
    """求单应矩阵系数: 把 from_quad 四点映射到 to_quad 四点。

    返回 8 系数 (a,b,c,d,e,f,g,h), 点映射为:
      X = (a*x + b*y + c) / (g*x + h*y + 1)
      Y = (d*x + e*y + f) / (g*x + h*y + 1)
    用高斯消元解 8x8 线性方程组 (纯 Python)。
    """
    A, bvec = [], []
    for (x, y), (X, Y) in zip(from_quad, to_quad):
        A.append([x, y, 1, 0, 0, 0, -X * x, -X * y]); bvec.append(X)
        A.append([0, 0, 0, x, y, 1, -Y * x, -Y * y]); bvec.append(Y)
    return _gauss(A, bvec)


def _gauss(A, b):
    n = 8
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(M[r][col]))
        M[col], M[piv] = M[piv], M[col]
        pv = M[col][col]
        if abs(pv) < 1e-12:
            raise ValueError('singular homography')
        for j in range(col, n + 1):
            M[col][j] /= pv
        for r in range(n):
            if r != col and M[r][col]:
                f = M[r][col]
                for j in range(col, n + 1):
                    M[r][j] -= f * M[col][j]
    return [M[i][n] for i in range(n)]


def apply_h(h, p):
    x, y = p
    a, b, c, d, e, f, g, hh = h
    den = g * x + hh * y + 1.0
    return ((a * x + b * y + c) / den, (d * x + e * y + f) / den)


def armor_source(color, digit=False):
    """生成矩形装甲板模板 (源坐标): 黑底 + 两侧 LED + 可选白数字。"""
    img = Image.new('RGB', (AW, AH), (0, 0, 0))
    d = ImageDraw.Draw(img)
    c = COLORS[color]
    y0, y1 = (AH - BAR_H) // 2, (AH + BAR_H) // 2
    d.rectangle((BAR_MARGIN, y0, BAR_MARGIN + BAR_W, y1), fill=c)           # 左灯条
    d.rectangle((AW - BAR_MARGIN - BAR_W, y0, AW - BAR_MARGIN, y1), fill=c)  # 右灯条
    if digit:
        d.rectangle((AW // 2 - 28, AH // 2 - 55, AW // 2 + 28, AH // 2 + 55),
                    outline=(240, 240, 240), width=8)  # 用"口"字近似数字
    return img


def make_quad(cx, cy, w, h, yaw, pitch, roll, jitter, rng):
    """由矩形 + 偏航/俯仰/横滚 造出梯形四点 (TL, BL, BR, TR)。"""
    hw, hh = w / 2.0, h / 2.0
    pts = [[-hw, -hh], [-hw, hh], [hw, hh], [hw, -hh]]
    for i in (2, 3):                       # 偏航: 压缩右侧高度 -> 近大远小
        pts[i][1] *= (1.0 - yaw)
    for i in (0, 3):                       # 俯仰: 压缩上边宽度
        pts[i][0] *= (1.0 - pitch)
    a = math.radians(roll)                 # 横滚: 整体旋转
    for p in pts:
        x, y = p
        p[0] = x * math.cos(a) - y * math.sin(a)
        p[1] = x * math.sin(a) + y * math.cos(a)
    for p in pts:                          # 小随机扰动, 避免过于规整
        p[0] += rng.uniform(-jitter, jitter)
        p[1] += rng.uniform(-jitter, jitter)
    return [(cx + x, cy + y) for x, y in pts]


SRC_QUAD = [(0, 0), (0, AH), (AW, AH), (AW, 0)]  # TL, BL, BR, TR


def line_intersect(p1, p2, p3, p4):
    x1, y1 = p1; x2, y2 = p2; x3, y3 = p3; x4, y4 = p4
    den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
    if abs(den) < 1e-9:
        return ((x1 + x3) / 2.0, (y1 + y3) / 2.0)
    px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / den
    py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / den
    return (px, py)


CONFIGS = [
    # name,            color, scale, yaw,  pitch, roll, bright, noise, digit, n
    ('red_front',      'red',  1.00, 0.00, 0.00,   0,  1.00,  0, False, 1),
    ('red_yaw_mild',   'red',  1.00, 0.18, 0.00,   0,  1.00,  0, False, 1),
    ('red_yaw_strong', 'red',  1.00, 0.35, 0.05,   0,  1.00,  0, False, 1),
    ('red_pitch',      'red',  1.00, 0.00, 0.22,   0,  1.00,  0, False, 1),
    ('red_roll',       'red',  1.00, 0.05, 0.00,  18,  1.00,  0, False, 1),
    ('red_near',       'red',  1.60, 0.10, 0.05,   5,  1.00,  0, False, 1),
    ('red_small_far',  'red',  0.35, 0.15, 0.05,   0,  1.00,  0, False, 1),
    ('red_dark',       'red',  1.00, 0.15, 0.00,   0,  0.45,  6, False, 1),
    ('red_bright',     'red',  1.00, 0.10, 0.00,   0,  1.60,  0, False, 1),
    ('red_noisy',      'red',  1.00, 0.12, 0.03,   6,  1.00, 22, False, 1),
    ('red_digit',      'red',  1.10, 0.10, 0.00,   0,  1.00,  0, True,  1),
    ('blue_front',     'blue', 1.00, 0.00, 0.00,   0,  1.00,  0, False, 1),
    ('blue_yaw_mild',  'blue', 1.00, 0.18, 0.00,   0,  1.00,  0, False, 1),
    ('blue_yaw_strong','blue', 1.00, 0.35, 0.05,   0,  1.00,  0, False, 1),
    ('blue_pitch',     'blue', 1.00, 0.00, 0.22,   0,  1.00,  0, False, 1),
    ('blue_roll',      'blue', 1.00, 0.05, 0.00, -18,  1.00,  0, False, 1),
    ('blue_near',      'blue', 1.60, 0.10, 0.05,  -5,  1.00,  0, False, 1),
    ('blue_small_far', 'blue', 0.35, 0.15, 0.05,   0,  1.00,  0, False, 1),
    ('blue_dark',      'blue', 1.00, 0.15, 0.00,   0,  0.45,  6, False, 1),
    ('blue_noisy',     'blue', 1.00, 0.12, 0.03,  -6,  1.00, 22, False, 1),
    ('scene_red_blue', 'red',  0.80, 0.12, 0.00,   0,  1.00,  0, False, 2),
]


def render_one(cfg, canvas_size, rng):
    name, color, scale, yaw, pitch, roll, bright, noise, digit, n = cfg
    W, H = canvas_size
    canvas = Image.new('RGB', (W, H), (0, 0, 0))
    w, h = AW * scale, AH * scale
    labels = []

    for k in range(n):
        armor_color = color if k == 0 else ('blue' if color == 'red' else 'red')
        src = armor_source(armor_color, digit)   # 每块板按自身颜色建模板
        if n == 1:
            cx = W / 2 + rng.uniform(-40, 40)
            cy = H / 2 + rng.uniform(-30, 30)
        else:
            cx = W * (0.33 if k == 0 else 0.67)
            cy = H / 2 + rng.uniform(-30, 30)
        quad = make_quad(cx, cy, w, h, yaw, pitch, roll, 3.0, rng)
        pil = solve_h(quad, SRC_QUAD)          # 输出像素 -> 源像素 (PIL 要求)
        warped = src.transform((W, H), Image.PERSPECTIVE, pil,
                               resample=Image.BILINEAR)
        canvas = ImageChops.lighter(canvas, warped)  # 黑底上叠加
        # 真值: 板中心 = 梯形对角线的交点
        center = line_intersect(quad[0], quad[2], quad[1], quad[3])
        labels.append((name, armor_color, center[0], center[1], quad, digit))

    if bright != 1.0:
        canvas = ImageEnhance.Brightness(canvas).enhance(bright)
    if noise:
        nz = Image.effect_noise((W, H), noise).convert('RGB')
        canvas = ImageChops.add(canvas, nz, scale=1.0, offset=-128)

    return canvas, labels


def contact_sheet(images, cols=5, tw=300):
    rows = (len(images) + cols - 1) // cols
    th = int(tw * images[0][1].height / images[0][1].width)
    sheet = Image.new('RGB', (cols * tw, rows * th), (30, 30, 30))
    d = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(images):
        r, c = divmod(i, cols)
        thumb = im.resize((tw, th), Image.BILINEAR)
        sheet.paste(thumb, (c * tw, r * th))
        d.text((c * tw + 4, r * th + 4), name, fill=(255, 255, 0))
    return sheet


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='synth_armor')
    ap.add_argument('--seed', type=int, default=0)
    ap.add_argument('--canvas', nargs=2, type=int, default=[1280, 720])
    args = ap.parse_args()

    rng = random.Random(args.seed)
    os.makedirs(args.out, exist_ok=True)
    rows, thumbs = [], []
    for cfg in CONFIGS:
        img, labels = render_one(cfg, tuple(args.canvas), rng)
        fname = cfg[0] + '.png'
        img.save(os.path.join(args.out, fname))
        thumbs.append((cfg[0], img))
        for (nm, col, cx, cy, quad, digit) in labels:
            q = [f'{p[0]:.1f},{p[1]:.1f}' for p in quad]
            rows.append([nm, col, f'{cx:.1f}', f'{cy:.1f}',
                         f'{quad[0][0]:.1f}', f'{quad[0][1]:.1f}',
                         f'{quad[1][0]:.1f}', f'{quad[1][1]:.1f}',
                         f'{quad[2][0]:.1f}', f'{quad[2][1]:.1f}',
                         f'{quad[3][0]:.1f}', f'{quad[3][1]:.1f}',
                         int(digit)])
        print(f'  wrote {fname:26s} color={cfg[1]:4s} scale={cfg[2]:.2f} '
              f'yaw={cfg[3]:.2f} pitch={cfg[4]:.2f} roll={cfg[5]:+d} '
              f'bright={cfg[6]:.2f} noise={cfg[7]:2d}')

    with open(os.path.join(args.out, 'labels.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['name', 'color', 'cx', 'cy',
                    'x0', 'y0', 'x1', 'y1', 'x2', 'y2', 'x3', 'y3', 'digit'])
        w.writerows(rows)

    sheet = contact_sheet(thumbs)
    sheet.save(os.path.join(args.out, 'contact_sheet.png'))
    print(f'\n共 {len(thumbs)} 张图, 真值 {len(rows)} 条 -> {args.out}/labels.csv')
    print(f'总览 -> {args.out}/contact_sheet.png')


if __name__ == '__main__':
    main()
