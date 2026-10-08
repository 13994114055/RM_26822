import sys, time
sys.path.insert(0, r'D:\RM\drone\.tools\pyserial')
import serial
try:
    s = serial.Serial('COM4', 115200, timeout=0.2, write_timeout=1)
except Exception as e:
    print('OPEN_FAILED:', repr(e))
    raise SystemExit(0)
print('OPEN_OK')
time.sleep(0.2)
s.reset_input_buffer()
s.write(b'#status\r\n')
end = time.time() + 3
buf = bytearray()
while time.time() < end:
    d = s.read(4096)
    if d:
        buf += d
print(buf.decode('utf-8','replace'))
s.close()
