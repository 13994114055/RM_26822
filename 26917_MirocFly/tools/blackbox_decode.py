#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
blackbox_decode.py — INAV blackbox v2 解码器 (纯 Python, 无第三方依赖)

用途: 解析 INAV firmware 记录的 blackbox ".TXT" 日志, 提取
      loopIteration / time / axisRate / axisP / axisI / axisD /
      gyroADC / accSmooth / attitude / motor / rcCommand 等字段,
      用于飞行复盘 (自稳/振荡/漂移/翻-自旋等)。

来源: 本会话 (MirocFly 2026-10-10) 现场为分析 160315/162041/164110/171606
      等日志而写的在线解码器, 现固化为可复用工具。格式依据 INAV/Betaflight
      `src/main/blackbox/*`(编码器) 与 blackbox-tools(解码器) 的约定。

用法:
  python3 blackbox_decode.py <log.TXT> [--list] [--index N]
          [--csv out.csv] [--spectrum FIELD] [--signed]
  例:
  python3 blackbox_decode.py ~/Downloads/blackbox_log_XXXXXXXX.TXT
  python3 blackbox_decode.py log.TXT --csv out.csv --spectrum gyroADC[0]

字段/编码/预测器 (与固件一致):
  encoding: 0 SIGNED_VB, 1 UNSIGNED_VB, 3 NEG_14BIT, 6 TAG8_8SVB,
            7 TAG2_3S32, 8 TAG8_4S16, 9 NULL(该帧不写)
  predictor: 0=0, 1=prev, 2=straight(2p1-p2), 3=avg2, 4=minthrottle,
             5=motor0, 6=inc, 7=home(0), 8=1500, 9=vbatref
"""
import sys, math, argparse

# ---------------- 基础读取 ----------------
class Reader:
    def __init__(self, data):
        self.d = data
        self.i = 0
    def eof(self):
        return self.i >= len(self.d)
    def u8(self):
        b = self.d[self.i]; self.i += 1; return b
    def find(self, seq):
        return self.d.find(seq, self.i)
    def readline(self):
        j = self.d.find(b'\n', self.i)
        if j < 0:
            line = self.d[self.i:]; self.i = len(self.d)
        else:
            line = self.d[self.i:j]; self.i = j + 1
        return line

def sx(v, bits):
    m = 1 << (bits - 1)
    return (v ^ m) - m

# ---------------- 变体/定点/打包 编码 ----------------
def read_unsigned_vb(r):
    v = 0; s = 0
    while True:
        b = r.u8()
        v |= (b & 0x7f) << s
        if not (b & 0x80):
            break
        s += 7
    return v

def read_signed_vb(r):
    u = read_unsigned_vb(r)
    return (u >> 1) ^ -(u & 1)

def read_neg_14bit(r):
    return -(read_unsigned_vb(r) & 0x3fff)

def read_tag2_3s32(r):        # 3 个有符号字段
    b0 = r.u8(); sel = (b0 >> 6) & 3; vals = [0, 0, 0]
    if sel == 0:
        vals = [sx((b0 >> 4) & 3, 2), sx((b0 >> 2) & 3, 2), sx(b0 & 3, 2)]
    elif sel == 1:
        b1 = r.u8(); vals = [sx(b0 & 0xf, 4), sx((b1 >> 4) & 0xf, 4), sx(b1 & 0xf, 4)]
    elif sel == 2:
        b1 = r.u8(); b2 = r.u8(); vals = [sx(b0 & 0x3f, 6), sx(b1, 8), sx(b2, 8)]
    else:
        s2 = b0 & 0x3f
        for x in range(3):
            nb = (s2 >> (2 * x)) & 3; v = 0
            for k in range(nb + 1):
                v |= r.u8() << (8 * k)
            if v >= 1 << (8 * (nb + 1) - 1):
                v -= 1 << (8 * (nb + 1))
            vals[x] = v
    return vals

def skip_tag8_4s16(r):        # 消费正确字节数; 值不重建(本工具不需要 rcData/rcCommand 值)
    sel = r.u8(); nib = 0
    for x in range(4):
        s = (sel >> (2 * x)) & 3
        if s == 1:
            if nib == 0: nib = 1
            else: r.u8(); nib = 0
        elif s == 2:
            r.u8()
        elif s == 3:
            r.u8(); r.u8()
    if nib == 1:
        r.u8()

def read_tag8_8svb(r, count):
    if count == 1:
        read_signed_vb(r); return [0]
    hdr = r.u8(); out = [0] * count
    for i in range(count):
        if (hdr >> i) & 1:
            read_signed_vb(r)     # 值不重建
    return out

# ---------------- 逐帧解码 (按头部编码表) ----------------
def read_values(r, encs):
    n = len(encs); out = [None] * n; i = 0
    while i < n:
        e = encs[i]
        if e == 0:   out[i] = read_signed_vb(r); i += 1
        elif e == 1: out[i] = read_unsigned_vb(r); i += 1
        elif e == 3: out[i] = read_neg_14bit(r); i += 1
        elif e == 7:
            vals = read_tag2_3s32(r); out[i:i+3] = vals; i += 3
        elif e == 8:
            skip_tag8_4s16(r); i += 4
        elif e == 6:
            j = i
            while j < n and encs[j] == 6: j += 1
            read_tag8_8svb(r, j - i); i = j
        elif e == 9:
            i += 1
        else:
            raise RuntimeError("encoding %d @%d" % (e, i))
    return out

def apply_pred(kind, i, h1, h2, cur, cfg):
    if kind == 0: return 0
    if kind == 1: return h1[i]
    if kind == 2: return 2 * h1[i] - h2[i]
    if kind == 3: return (h1[i] + h2[i]) // 2
    if kind == 4: return cfg['minthrottle']
    if kind == 5: return cur[cfg['motor0']] if cfg.get('motor0') is not None else 0
    if kind == 6: return h1[i] + 1
    if kind == 7: return 0
    if kind == 8: return 1500
    if kind == 9: return cfg['vbatref']
    return 0

# ---------------- 头部 ----------------
def parse_header_line(line, meta):
    if not line.startswith(b'H'):
        return None
    body = line[1:].strip()
    k, _, v = body.partition(b':')
    try:
        k = k.decode('latin1').strip(); v = v.decode('latin1').strip()
    except Exception:
        return None
    meta[k] = v
    return k

def get(name, meta, default=None):
    return meta.get(name, default)

# ---------------- 主解码 ----------------
def decode(path, index=None):
    data = open(path, 'rb').read()
    r = Reader(data)
    meta = {}
    # 初始头部
    while not r.eof() and data[r.i] == ord('H'):
        r.i += 1
        parse_header_line(b'H' + r.readline(), meta)
    if 'Field I name' not in meta:
        raise SystemExit("未找到 'Field I name' 头部: 不是 INAV blackbox 日志?")

    names = meta['Field I name'].split(',')
    n = len(names)
    IX = {nm: i for i, nm in enumerate(names)}
    Ipred = [int(x) for x in meta['Field I predictor'].split(',')]
    Ienc = [int(x) for x in meta['Field I encoding'].split(',')]
    Ppred = [int(x) for x in meta['Field P predictor'].split(',')]
    Penc = [int(x) for x in meta['Field P encoding'].split(',')]
    Senc = [int(x) for x in meta['Field S encoding'].split(',')] if 'Field S encoding' in meta else []

    cfg = {
        'minthrottle': int(meta.get('minthrottle', '1080')),
        'vbatref': int(meta.get('vbatref', '36')),
        'motor0': IX.get('motor[0]'),
    }

    # 逐段(每个 'H Product' 视为一段) 解码
    h1 = [0] * n; h2 = [0] * n
    logs = []; cur = None
    desync = 0
    while not r.eof():
        m = data[r.i]
        if m == ord('H'):
            r.i += 1
            if parse_header_line(b'H' + r.readline(), meta) == 'Product':
                h1 = [0] * n; h2 = [0] * n; cur = None   # 新的一段日志
        elif m in (ord('I'), ord('P')):
            r.i += 1
            intr = (m == ord('I'))
            try:
                dl = read_values(r, Ienc if intr else Penc)
                c = [0] * n
                for k in range(n):
                    p = apply_pred((Ipred if intr else Ppred)[k], k, h1, h2, c, cfg)
                    c[k] = p if dl[k] is None else p + dl[k]
            except Exception:
                desync += 1; r.i += 1
                j = data.find(b'H Product:Blackbox', r.i)
                if j < 0: break
                r.i = j; continue
            if cur is None:
                cur = []; logs.append(cur)
            cur.append((chr(m), c))
            if intr: h1 = list(c); h2 = list(c)
            else: h2 = h1; h1 = list(c)
        elif m == ord('S'):
            r.i += 1
            try: read_values(r, Senc) if Senc else None
            except Exception: r.i += 1
        elif m == ord('E'):
            r.i += 1
            t = r.u8()
            sk = {0: 4, 15: 4, 30: 8, 14: 8}.get(t)
            if t == 255 or sk is None:
                j = data.find(b'\x00', r.i); r.i = (j + 1) if j >= 0 else r.i
            else:
                r.i += sk
        elif m == ord('G'):
            j = data.find(b'H Product:Blackbox', r.i); r.i = len(data) if j < 0 else j
        else:
            desync += 1; r.i += 1
            j = data.find(b'H Product:Blackbox', r.i)
            if j > 0: r.i = j
    return names, IX, logs, desync, meta

# ---------------- 输出 ----------------
def summarize(lf, IX, idx):
    T = [c[IX['time']] for _, c in lf]
    t0 = T[0]
    def col(field, div=1.0):
        if field in IX:
            return [c[IX[field]] / div for _, c in lf]
        return []
    roll = col('attitude[0]', 10.0); pitch = col('attitude[1]', 10.0)
    m = [col('motor[%d]' % k) for k in range(4)]
    print("\n=== 日志 #%d: %d 帧, 时长 %.2fs ===" % (idx, len(lf), (max(T) - t0) / 1e6))
    if roll and pitch:
        print("  attitude(deg): roll %.1f..%.1f  pitch %.1f..%.1f" % (min(roll), max(roll), min(pitch), max(pitch)))
    if m and m[0]:
        print("  motor: m0 %d..%d  m1 %d..%d  m2 %d..%d  m3 %d..%d" %
              (min(m[0]), max(m[0]), min(m[1]), max(m[1]), min(m[2]), max(m[2]), min(m[3]), max(m[3])))
    print("  t_s     roll  pitch   yaw   gZ | m0   m1   m2   m3")
    step = max(1, len(lf) // 30)
    for i in range(0, len(lf), step):
        c = lf[i][1]
        print("  %6.2f %6.1f %6.1f %6.1f %4d | %4d %4d %4d %4d" % (
            (T[i] - t0) / 1e6,
            c[IX['attitude[0]']] / 10.0, c[IX['attitude[1]']] / 10.0, c[IX['attitude[2]']] / 10.0,
            c[IX['gyroADC[2]']],
            c[IX['motor[0]']], c[IX['motor[1]']], c[IX['motor[2]']], c[IX['motor[3]']]))

def export_csv(lf, names, IX, path):
    import csv
    with open(path, 'w', newline='') as f:
        w = csv.writer(f); w.writerow(names)
        for _, c in lf:
            w.writerow(c)
    print("CSV 已写: %s (%d 帧)" % (path, len(lf)))

def fft(a):
    N = len(a)
    import cmath
    A = [complex(x) for x in a]
    j = 0
    for i in range(1, N):
        bit = N >> 1
        while j & bit: j ^= bit; bit >>= 1
        j ^= bit
        if i < j: A[i], A[j] = A[j], A[i]
    ln = 2
    while ln <= N:
        wl = cmath.exp(-2j * cmath.pi / ln)
        for i in range(0, N, ln):
            w = 1 + 0j
            for k in range(i, i + ln // 2):
                u = A[k]; v = A[k + ln // 2] * w
                A[k] = u + v; A[k + ln // 2] = u - v; w *= wl
        ln <<= 1
    return A

def spectrum(lf, IX, field):
    if field not in IX:
        print("无字段 %s" % field); return
    a = [(c[IX['time']], c[IX[field]]) for _, c in lf]
    import statistics
    t = [x[0] for x in a]; v = [x[1] for x in a]
    dt = statistics.median([t[i+1] - t[i] for i in range(len(t)-1)])
    N = 2048
    grid = [t[0] + i * dt for i in range(N)]
    k = 0; s = []
    for x in grid:
        while k < len(t) - 2 and t[k+1] < x: k += 1
        s.append(v[k] + (v[k+1] - v[k]) * (x - t[k]) / (t[k+1] - t[k]) if t[k+1] != t[k] else v[k])
    m = sum(s) / N; s = [(x - m) * (0.5 - 0.5 * math.cos(2 * math.pi * i / (N - 1))) for i, x in enumerate(s)]
    F = fft(s); half = N // 2
    mag = [abs(F[i]) for i in range(1, half)]
    pk = max(range(len(mag)), key=lambda i: mag[i]) + 1
    fpk = pk / (N * dt / 1e6)
    print("频谱 %s: 主频 %.1f Hz (dt=%.0fus, N=%d)" % (field, fpk, dt, N))

def main():
    ap = argparse.ArgumentParser(description="INAV blackbox v2 解码器")
    ap.add_argument('log')
    ap.add_argument('--list', action='store_true', help='只列出各段帧数')
    ap.add_argument('--index', type=int, default=None, help='选择第 N 段(0基); 默认取帧数最多的一段')
    ap.add_argument('--csv', help='导出所选段全部字段到 CSV')
    ap.add_argument('--spectrum', help='对某字段做频谱(如 gyroADC[0])')
    a = ap.parse_args()

    names, IX, logs, desync, meta = decode(a.log)
    logs = [x for x in logs if len(x) > 50]
    print("字段数 %d | 段数 %d | desync %d" % (len(names), len(logs), desync))
    print("固件: %s" % get('Firmware revision', meta, '?'))
    print("段帧数: %s" % [len(x) for x in logs])

    if a.list or not logs:
        return
    idx = a.index if a.index is not None else max(range(len(logs)), key=lambda i: len(logs[i]))
    if idx >= len(logs):
        raise SystemExit("index 越界")
    lf = logs[idx]
    summarize(lf, IX, idx)
    if a.csv:
        export_csv(lf, names, IX, a.csv)
    if a.spectrum:
        spectrum(lf, IX, a.spectrum)

if __name__ == '__main__':
    main()
