import sys, time
sys.path.insert(0, r'D:\RM\drone\.tools\pyserial')
import serial
s = serial.Serial('COM4', 115200, timeout=0.2, write_timeout=1)
time.sleep(0.5)
s.reset_input_buffer()
for cmd in [b'\r\n', b'#\r\n', b'status\r\n', b'help\r\n']:
    print('>>>', repr(cmd))
    s.write(cmd); s.flush()
    end=time.time()+2
    buf=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if d: buf += d
    print(buf.decode('utf-8','replace'))
s.close()
