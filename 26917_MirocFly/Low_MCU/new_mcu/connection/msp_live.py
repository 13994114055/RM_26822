#!/usr/bin/env python3
# msp_live.py - 实时读取 INAV 飞控的 ATTITUDE / MOTOR / RAW_IMU (MSP v1 over USB VCP)
# 用法: python3 msp_live.py /dev/ttyACM0 [秒数]
import os, sys, time, select, termios, glob

def find_port(p):
    if p and os.path.exists(p):
        return p
    ps = sorted(glob.glob('/dev/ttyACM*'))
    if not ps:
        sys.exit("no ttyACM*")
    return ps[0]

def open_port(dev):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd

def msp_frame(cmd, data=b''):
    csum = len(data) ^ cmd
    for b in data:
        csum ^= b
    return b'$M<' + bytes([len(data) & 0xFF, cmd & 0xFF]) + data + bytes([csum & 0xFF])

MSP_MOTOR = 104
MSP_RAW_IMU = 102
MSP_ATTITUDE = 108
MSP_RC = 105

class Parser:
    def __init__(self): self.buf = b''
    def feed(self, d):
        self.buf += d
        out = []
        while True:
            i = self.buf.find(b'$M')
            if i < 0: self.buf = b''; break
            self.buf = self.buf[i:]
            if len(self.buf) < 6: break
            d0 = self.buf[2]
            ln = self.buf[3]; cmd = self.buf[4]
            if len(self.buf) < 5 + ln + 1: break
            payload = self.buf[5:5+ln]
            csum = self.buf[5+ln]
            c = ln ^ cmd
            for b in payload: c ^= b
            if c == csum:
                out.append((d0, cmd, payload))
            self.buf = self.buf[5+ln+1:]
        return out

def main():
    dev = find_port(sys.argv[1] if len(sys.argv) > 1 else None)
    dur = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    fd = open_port(dev)
    print("opened", dev, "for", dur, "s")
    p = Parser()
    t0 = time.time()
    last = {}
    last_motor = None
    while time.time() - t0 < dur:
        for cmd in (MSP_ATTITUDE, MSP_MOTOR, MSP_RAW_IMU, MSP_RC):
            os.write(fd, msp_frame(cmd))
        end = time.time() + 0.1
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.02)
            if not r: continue
            try: d = os.read(fd, 4096)
            except OSError: break
            for (d0, cmd, pl) in p.feed(d):
                last[cmd] = pl
        att = last.get(MSP_ATTITUDE, b'')
        mot = last.get(MSP_MOTOR, b'')
        rc = last.get(MSP_RC, b'')
        imu = last.get(MSP_RAW_IMU, b'')
        if len(att) >= 6:
            r_ = int.from_bytes(att[0:2], 'little', signed=True)
            p_ = int.from_bytes(att[2:4], 'little', signed=True)
            y_ = int.from_bytes(att[4:6], 'little', signed=True)
        else:
            r_ = p_ = y_ = None
        ms = []
        for i in range(0, min(len(mot), 16), 2):
            ms.append(int.from_bytes(mot[i:i+2], 'little'))
        # MSP_RC: byte0 = channel count, then channels uint16
        rcs = []
        if len(rc) >= 1:
            cnt = rc[0]
            for i in range(0, min(cnt, 8)):
                off = 1 + i*2
                if off+2 <= len(rc):
                    rcs.append(int.from_bytes(rc[off:off+2], 'little'))
        gx = gy = gz = None
        if len(imu) >= 12:
            gx = int.from_bytes(imu[6:8], 'little', signed=True)
            gy = int.from_bytes(imu[8:10], 'little', signed=True)
            gz = int.from_bytes(imu[10:12], 'little', signed=True)
        print("att r=%6s p=%6s y=%6s | g r=%5s p=%5s y=%5s | mot=%s | rc=%s" % (
            r_, p_, y_, gx, gy, gz,
            ' '.join('%4d' % m for m in ms[:4]),
            ' '.join('%4d' % v for v in rcs)))
    os.close(fd)

if __name__ == '__main__':
    main()
