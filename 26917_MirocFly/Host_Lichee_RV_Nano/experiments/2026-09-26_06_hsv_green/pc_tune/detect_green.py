#!/usr/bin/env python3
"""实验06 PC 调参: 对板上 NV21 raw 做绿色荧光检测, 输出最大块质心/面积/偏移 + HSV 统计。

用法:
  python3 detect_green.py <in.raw> <width> <height> [out.bmp]
  可选覆盖阈值:  --h HMIN HMAX --s SMIN SMAX --v VMIN VMAX
  可选: --scale N (连通块降采样倍数, 默认4)  --mask (另存 mask.bmp)

PIL 的 HSV: H/S/V 均 0-255。绿色 H 大约 40-110。
"""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

DEF_H = (40, 110)
DEF_S = (60, 255)
DEF_V = (60, 255)
SCALE = 4


def nv21_to_rgb(data, w, h):
    y = data[:w * h]
    uv = data[w * h:w * h + (w * h) // 2]
    ycbcr = bytearray(w * h * 3)
    for j in range(h):
        base = j * w
        cbase = (j // 2) * w
        for i in range(w):
            k = base + i
            ycbcr[k * 3] = y[k]
            ci = cbase + (i // 2) * 2
            ycbcr[k * 3 + 1] = uv[ci + 1]  # Cb = U
            ycbcr[k * 3 + 2] = uv[ci]      # Cr = V
    return Image.frombytes('YCbCr', (w, h), bytes(ycbcr)).convert('RGB')


def build_mask(hsv, hmin, hmax, smin, smax, vmin, vmax):
    hch, sch, vch = hsv.split()
    hm = hch.point(lambda p: 255 if hmin <= p <= hmax else 0)
    sm = sch.point(lambda p: 255 if smin <= p <= smax else 0)
    vm = vch.point(lambda p: 255 if vmin <= p <= vmax else 0)
    m = ImageChops.multiply(ImageChops.multiply(hm, sm), vm)
    m = m.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))
    return m


def largest_component(mask_small, ws, hs):
    px = mask_small.load()
    seen = bytearray(ws * hs)
    best = (0, None, 0, 0)  # cnt, bbox, sumx, sumy
    for y0 in range(hs):
        for x0 in range(ws):
            if seen[y0 * ws + x0] or not px[x0, y0]:
                continue
            stack = [(x0, y0)]
            seen[y0 * ws + x0] = 1
            cnt = 0
            minx = maxx = x0
            miny = maxy = y0
            sumx = sumy = 0
            while stack:
                x, y = stack.pop()
                cnt += 1
                sumx += x
                sumy += y
                if x < minx: minx = x
                if x > maxx: maxx = x
                if y < miny: miny = y
                if y > maxy: maxy = y
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < ws and 0 <= ny < hs:
                        ni = ny * ws + nx
                        if not seen[ni] and px[nx, ny]:
                            seen[ni] = 1
                            stack.append((nx, ny))
            if cnt > best[0]:
                best = (cnt, (minx, miny, maxx, maxy), sumx, sumy)
    return best


def parse_args(argv):
    if len(argv) < 4:
        print(__doc__)
        sys.exit(1)
    raw, w, h = argv[1], int(argv[2]), int(argv[3])
    out = argv[4] if len(argv) > 4 and not argv[4].startswith('--') else 'detect.bmp'
    hmin, hmax = DEF_H
    smin, smax = DEF_S
    vmin, vmax = DEF_V
    scale = SCALE
    save_mask = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--h': hmin, hmax = int(argv[i+1]), int(argv[i+2]); i += 3
        elif a == '--s': smin, smax = int(argv[i+1]), int(argv[i+2]); i += 3
        elif a == '--v': vmin, vmax = int(argv[i+1]), int(argv[i+2]); i += 3
        elif a == '--scale': scale = int(argv[i+1]); i += 2
        elif a == '--mask': save_mask = True; i += 1
        else: i += 1
    return raw, w, h, out, (hmin, hmax, smin, smax, vmin, vmax), scale, save_mask


def main():
    raw, w, h, out, (hmin, hmax, smin, smax, vmin, vmax), scale, save_mask = parse_args(sys.argv)
    data = open(raw, 'rb').read()
    print(f"loaded {raw}: {len(data)} bytes ({w}x{h})")
    rgb = nv21_to_rgb(data, w, h)
    hsv = rgb.convert('HSV')
    mask = build_mask(hsv, hmin, hmax, smin, smax, vmin, vmax)
    if save_mask:
        mask.save('mask.bmp')

    ws, hs = max(1, w // scale), max(1, h // scale)
    msmall = mask.resize((ws, hs), Image.NEAREST)
    cnt, bbox, sumx, sumy = largest_component(msmall, ws, hs)

    draw = ImageDraw.Draw(rgb)
    cx0, cy0 = w // 2, h // 2
    draw.line((cx0, cy0 - 40, cx0, cy0 + 40), fill=(0, 0, 255), width=4)
    draw.line((cx0 - 40, cy0, cx0 + 40, cy0), fill=(0, 0, 255), width=4)

    if not bbox:
        print("no green found (调 --h/--s/--v)")
        rgb.save(out)
        print(f"wrote {out}")
        return

    x0, y0, x1, y1 = bbox
    fx0, fy0, fx1, fy1 = x0 * scale, y0 * scale, (x1 + 1) * scale, (y1 + 1) * scale
    fcx, fcy = int(sumx / cnt * scale), int(sumy / cnt * scale)
    area = cnt * scale * scale
    offx, offy = fcx - cx0, fcy - cy0
    print(f"GREEN largest: bbox=({fx0},{fy0},{fx1},{fy1}) centroid=({fcx},{fcy}) "
          f"offset=({offx},{offy}) area~{area}")

    # 在最大块的 bbox 内统计真实 HSV, 帮助收紧阈值
    hch, sch, vch = hsv.split()
    hp, sp, vp, mp = hch.load(), sch.load(), vch.load(), mask.load()
    corners = [90]
    Hs, Ss, Vs = [], [], []
    step = max(1, (fx1 - fx0) // 64)
    for yy in range(fy0, fy1, max(1, step)):
        for xx in range(fx0, fx1, max(1, step)):
            if mp[xx, yy]:
                Hs.append(hp[xx, yy]); Ss.append(sp[xx, yy]); Vs.append(vp[xx, yy])
    if Hs:
        def rng(a):
            a = sorted(a)
            return a[0], a[len(a)//2], a[-1], a[int(len(a)*0.05)], a[int(len(a)*0.95)]
        for name, a in (("H", Hs), ("S", Ss), ("V", Vs)):
            lo, med, hi, p5, p95 = rng(a)
            print(f"  {name}: min={lo} med={med} max={hi}  p5={p5} p95={p95}")

    draw.rectangle((fx0, fy0, fx1, fy1), outline=(255, 0, 0), width=6)
    draw.ellipse((fcx - 8, fcy - 8, fcx + 8, fcy + 8), outline=(0, 255, 255), width=4)
    rgb.save(out)
    print(f"wrote {out}")


if __name__ == '__main__':
    main()
