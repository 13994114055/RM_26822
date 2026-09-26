#!/usr/bin/env python3
"""NV21 raw -> BMP 转换 (零依赖, 便于在 PC 查看板上抓的帧)
用法: python3 nv21_to_bmp.py <in.raw> <width> <height> <out.bmp>
假设 raw 是紧密打包的 NV21 (stride == width), 1280/640 等 64 对齐分辨率成立
"""
import sys

def convert(nv21, w, h):
    y = nv21[:w * h]
    uv = nv21[w * h:]
    row_size = ((w * 3 + 3) // 4) * 4
    rgb = bytearray(row_size * h)
    for j in range(h):
        for i in range(w):
            yy = y[j * w + i]
            uv_i = (j // 2) * w + (i // 2) * 2
            v = uv[uv_i]
            u = uv[uv_i + 1]
            c = yy - 16
            d = u - 128
            e = v - 128
            r = max(0, min(255, (298 * c + 409 * e + 128) >> 8))
            g = max(0, min(255, (298 * c - 100 * d - 208 * e + 128) >> 8))
            b = max(0, min(255, (298 * c + 516 * d + 128) >> 8))
            off = j * row_size + i * 3
            rgb[off] = b
            rgb[off + 1] = g
            rgb[off + 2] = r
    bmp = bytearray(b'BM')
    filesize = 54 + len(rgb)
    bmp += filesize.to_bytes(4, 'little')
    bmp += (0).to_bytes(4, 'little')
    bmp += (54).to_bytes(4, 'little')
    bmp += (40).to_bytes(4, 'little')
    bmp += w.to_bytes(4, 'little')
    bmp += h.to_bytes(4, 'little')
    bmp += (1).to_bytes(2, 'little')
    bmp += (24).to_bytes(2, 'little')
    bmp += (0).to_bytes(4, 'little')
    bmp += len(rgb).to_bytes(4, 'little')
    bmp += (2835).to_bytes(4, 'little')
    bmp += (2835).to_bytes(4, 'little')
    bmp += (0).to_bytes(4, 'little')
    bmp += (0).to_bytes(4, 'little')
    bmp += bytes(rgb)
    return bmp

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print("usage: nv21_to_bmp.py <in.raw> <width> <height> <out.bmp>")
        sys.exit(1)
    with open(sys.argv[1], 'rb') as f:
        data = f.read()
    w, h = int(sys.argv[2]), int(sys.argv[3])
    out = convert(data, w, h)
    with open(sys.argv[4], 'wb') as f:
        f.write(out)
    print(f"wrote {sys.argv[4]} ({len(out)} bytes)")