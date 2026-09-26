#!/usr/bin/env python3
"""对板上抓的 NV21 raw 做绿色荧光目标检测 (PC 端调阈值, 基于 PIL)

用法: python3 detect_green.py <in.raw> <width> <height> [out.bmp]
输出: 打印最大绿色块的 中心偏移/面积, 并保存带框的 BMP
"""
import sys
from PIL import Image, ImageDraw

# 绿色 HSV 阈值 (PIL: H 0-255, S 0-255, V 0-255)
# 绿色在 PIL 色相环约 60-110; 荧光绿偏亮, V 要高
H_MIN, H_MAX = 40, 110
S_MIN, S_MAX = 80, 255
V_MIN, V_MAX = 80, 255


def nv21_to_rgb(data, w, h):
    y = data[:w * h]
    uv = data[w * h:w * h + (w * h) // 2]
    # NV21: VU 交错 -> YCbCr 每像素 3 字节 (Y,Cb,Cr); 色度半分辨率, 每 2x2 采样
    ycbcr = bytearray(w * h * 3)
    for i in range(w * h):
        ycbcr[i * 3] = y[i]
        ycbcr[i * 3 + 1] = uv[(i // 2) * 2 + 1]  # Cb = U
        ycbcr[i * 3 + 2] = uv[(i // 2) * 2]      # Cr = V
    return Image.frombytes('YCbCr', (w, h), bytes(ycbcr)).convert('RGB')


def main():
    if len(sys.argv) < 4:
        print("usage: detect_green.py <in.raw> <width> <height> [out.bmp]")
        sys.exit(1)
    raw, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    out = sys.argv[4] if len(sys.argv) > 4 else "detect.bmp"

    with open(raw, 'rb') as f:
        data = f.read()
    print(f"loaded {raw}: {len(data)} bytes ({w}x{h})")

    rgb = nv21_to_rgb(data, w, h)
    hsv = rgb.convert('HSV')

    # 阈值 -> 二值 mask (用 PIL point 逐通道)
    hch, sch, vch = hsv.split()
    hmask = hch.point(lambda p: 255 if H_MIN <= p <= H_MAX else 0)
    smask = sch.point(lambda p: 255 if S_MIN <= p <= S_MAX else 0)
    vmask = vch.point(lambda p: 255 if V_MIN <= p <= V_MAX else 0)
    from PIL import ImageChops
    mask = ImageChops.multiply(ImageChops.multiply(hmask, smask), vmask)

    bbox = mask.getbbox()
    draw = ImageDraw.Draw(rgb)
    if bbox:
        x0, y0, x1, y1 = bbox
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        area = (x1 - x0) * (y1 - y0)
        offx, offy = cx - w // 2, cy - h // 2
        print(f"GREEN bbox={bbox} center=({cx},{cy}) offset=({offx},{offy}) area={area}")
        draw.rectangle(bbox, outline=(255, 0, 0), width=8)
        draw.line((w // 2, h // 2 - 40, w // 2, h // 2 + 40), fill=(0, 0, 255), width=4)
        draw.line((w // 2 - 40, h // 2, w // 2 + 40, h // 2), fill=(0, 0, 255), width=4)
    else:
        print("no green found (adjust HSV thresholds)")

    rgb.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()