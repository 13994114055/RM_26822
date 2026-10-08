import sys, time
sys.path.insert(0, r'D:\RM\drone\.tools\pyserial')
import serial
PORT='COM4'; BAUD=115200

def xmodem(data):
    crc=0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc=((crc<<1)^0x1021)&0xffff if crc&0x8000 else (crc<<1)&0xffff
    return crc

def fw(cmd,params=b'\x00',addr=0):
    p=bytes(params); body=bytes([0x2f,cmd,(addr>>8)&255,addr&255,len(p)&255])+p; c=xmodem(body); return body+bytes([c>>8,c&255])

def read_until_prompt(s,timeout=4):
    end=time.time()+timeout; b=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if d:
            b+=d
            if b.endswith(b'# '): break
    return bytes(b)

def enter_cli(s):
    s.reset_input_buffer(); s.write(b'\r\n'); s.flush(); time.sleep(.1)
    b=read_until_prompt(s,1)
    if not b.endswith(b'# '): s.write(b'#\r\n'); s.flush(); b+=read_until_prompt(s,4)
    return b

def cli(s,text,timeout=4):
    s.reset_input_buffer(); s.write(text.encode()+b'\r\n'); s.flush()
    end=time.time()+timeout; b=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if d:
            b+=d
            if b.endswith(b'# '): break
    return bytes(b)

def read_msp(s,timeout=4):
    end=time.time()+timeout; b=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if d: b+=d
        i=b.find(b'$M>')
        if i>=0 and len(b)>=i+6:
            n=b[i+3]
            if len(b)>=i+6+n:
                f=bytes(b[i:i+6+n]); return f,f[5:5+n],bytes(b[:i])
    return bytes(b),None,bytes(b)

def read_4way(s,timeout=4):
    end=time.time()+timeout; b=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if d: b+=d
        i=b.find(b'\x2e')
        if i>=0:
            if len(b)<i+5: continue
            n=b[i+4] or 256
            if len(b)>=i+8+n:
                f=bytes(b[i:i+8+n]); p=f[5:5+n]; a=f[5+n]; c=(f[6+n]<<8)|f[7+n]
                return {'cmd':f[1],'addr':(f[2]<<8)|f[3],'len':n,'payload':p,'ack':a,'crc_ok':c==xmodem(f[:6+n]),'frame':f}, bytes(b[:i])
    return None,bytes(b)

def fourway(s,cmd,params=b'\x00',addr=0,timeout=4):
    s.reset_input_buffer(); s.write(fw(cmd,params,addr)); s.flush(); return read_4way(s,timeout)

s=serial.Serial(PORT,BAUD,timeout=.15,write_timeout=1)
print('OPEN1',flush=True)
print(enter_cli(s).decode('utf-8','replace'),flush=True)
print(cli(s,'status').decode('utf-8','replace'),flush=True)
print(cli(s,'set debug_mode = ESC').decode('utf-8','replace'),flush=True)
print(cli(s,'get debug_mode').decode('utf-8','replace'),flush=True)
print(cli(s,'exit',1).decode('utf-8','replace'),flush=True)
time.sleep(.25)
msp=bytes([0x24,0x4d,0x3c,0x00,0xf5,0xf5])
s.reset_input_buffer(); s.write(msp); s.flush()
f,p,pre=read_msp(s,4)
print('MSP PASS frame=%s payload=%s prefix=%s' % (f.hex(' '),p.hex(' ') if p else None,pre.hex(' ')),flush=True)
if p is not None:
    for cmd,name in [(0x31,'protocol'),(0x32,'name'),(0x33,'ifver')]:
        r,noise=fourway(s,cmd,b'\x00',0,3)
        print(name,r,'noise=',noise.hex(' '),flush=True)
    s.reset_input_buffer(); s.write(fw(0x34,b'\x00')); s.flush(); time.sleep(.8)
    print('EXIT SENT',flush=True)
s.close(); print('CLOSE1',flush=True)
time.sleep(1.5)
try:
    s2=serial.Serial(PORT,BAUD,timeout=.2,write_timeout=1)
    print('REOPEN_OK',flush=True)
    print(enter_cli(s2).decode('utf-8','replace'),flush=True)
    print(cli(s2,'status').decode('utf-8','replace'),flush=True)
    cli(s2,'exit',1); s2.close()
except Exception as e:
    print('REOPEN_FAILED',repr(e),flush=True)
