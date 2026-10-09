#!/usr/bin/env python3
"""fccli.py — 通过 USB VCP 与 INAV 飞控 CLI 交互（纯 stdlib termios，无需 pyserial）。

用途：给下位机 BetaFPV G473 读状态 / 粘配置。
用法：
  python3 fccli.py --cmd version --cmd status        # 只读探测
  python3 fccli.py --config ../drone/config/g473_msprc.txt   # 逐行执行(+save 在文件里)
说明：本机无 uucp 权限，故通过 docker 带 --device 运行（见 connection/README）。
"""
import os
import sys
import time
import select
import argparse
import termios


def open_serial(port, baud=115200):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    iflag = 0
    oflag = 0
    lflag = 0
    cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
    b = getattr(termios, 'B%d' % baud, termios.B115200)
    termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, b, b, cc])
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def read_all(fd, timeout=1.0, quiet=0.3):
    buf = b''
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                break
            if d:
                buf += d
                end = time.time() + quiet
    return buf


def wr(fd, s):
    os.write(fd, s.encode() if isinstance(s, str) else s)
    time.sleep(0.15)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--cmd', action='append', default=[])
    ap.add_argument('--config')
    ap.add_argument('--enter-only', action='store_true')
    ap.add_argument('--raw', type=float, default=0, help='只读原始串口 N 秒(不发送)')
    a = ap.parse_args()

    fd = open_serial(a.port, a.baud)
    if a.raw:
        sys.stdout.write(read_all(fd, a.raw, quiet=a.raw).decode(errors='replace'))
        os.close(fd)
        return
    wr(fd, '#\r\n')                       # 进入 CLI
    time.sleep(0.5)
    sys.stdout.write(read_all(fd, 2.0).decode(errors='replace'))
    if a.enter_only:
        os.close(fd)
        return

    cmds = []
    if a.config:
        for line in open(a.config):
            line = line.split('#', 1)[0].strip()   # 去掉行内注释(避免干扰 serial/aux 解析)
            if not line:
                continue
            cmds.append(line)
    cmds += a.cmd

    for c in cmds:
        wr(fd, c + '\r\n')
        out = read_all(fd, 1.2)
        sys.stdout.write('>>> ' + c + '\n')
        sys.stdout.write(out.decode(errors='replace'))
    os.close(fd)


if __name__ == '__main__':
    main()
