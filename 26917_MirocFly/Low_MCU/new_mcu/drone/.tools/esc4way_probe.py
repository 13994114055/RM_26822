import sys, time, struct
sys.path.insert(0, r'D:\RM\drone\.tools\pyserial')
import serial

PORT='COM4'
BAUD=115200

def xmodem(data):
    crc=0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc

def fw(cmd, params=b'\x00', addr=0):
    p=bytes(params)
    body=bytes([0x2f,cmd,(addr>>8)&0xff,addr&0xff,len(p)&0xff])+p
    crc=xmodem(body)
    return body+bytes([(crc>>8)&0xff,crc&0xff])

def read_cli(s, timeout=4):
    end=time.time()+timeout; out=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if d:
            out+=d
            if b'# ' in out[-64:]: break
    return bytes(out)

def enter_cli(s):
    s.reset_input_buffer(); s.write(b'#'); s.flush(); time.sleep(.15)
    return read_cli(s, 5)

def cli(s, cmd, timeout=4):
    s.reset_input_buffer(); s.write(cmd.encode()+b'\r\n'); s.flush()
    out=bytearray(); end=time.time()+timeout
    while time.time()<end:
        d=s.read(4096)
        if d:
            out+=d
            if out.count(b'# ')>=1 and (b'\r\n# ' in out[-96:] or out.rstrip().endswith(b'# ')): break
    return bytes(out)

def read_msp_reply(s, timeout=4):
    end=time.time()+timeout; buf=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if not d: continue
        buf+=d
        i=buf.find(b'$M>')
        if i<0: continue
        if len(buf)<i+6: continue
        n=buf[i+3]
        if len(buf)<i+6+n: continue
        frame=bytes(buf[i:i+6+n])
        payload=frame[5:5+n]
        return frame,payload
    return bytes(buf),None

def read_4way(s, timeout=3):
    end=time.time()+timeout; buf=bytearray()
    while time.time()<end:
        d=s.read(4096)
        if d:
            buf+=d
            i=buf.find(b'\x2e')
            if i<0: continue
            if len(buf)<i+6: continue
            n=buf[i+4]
            if n==0: n=256
            total=8+n
            if len(buf)<i+total: continue
            frame=bytes(buf[i:i+total])
            payload=frame[5:5+n]
            ack=frame[5+n]
            crc=(frame[6+n]<<8)|frame[7+n]
            calc=xmodem(frame[:6+n])
            return dict(frame=frame,cmd=frame[1],addr=(frame[2]<<8)|frame[3],payload=payload,ack=ack,crc_ok=crc==calc,crct=(crc,calc)),bytes(buf[:i])
    return None,bytes(buf)

def send_4way(s,cmd,params=b'\x00',addr=0,wait=3):
    pkt=fw(cmd,params,addr)
    s.reset_input_buffer(); s.write(pkt); s.flush()
    return read_4way(s,wait)

def enter_passthrough(s):
    frame=bytes([0x24,0x4d,0x3c,0x00,0xf5,0xf5])
    s.reset_input_buffer(); s.write(frame); s.flush()
    return read_msp_reply(s,4)

def exit_passthrough(s):
    s.write(fw(0x34,b'\x00')); s.flush(); time.sleep(.8)

s=serial.Serial(PORT,BAUD,timeout=.15,write_timeout=1)
print('OPEN',PORT)
print('ENTER_CLI_BYTES',len(enter_cli(s)))
print(cli(s,'set debug_mode = ESC').decode('utf-8','replace')[-500:])
print(cli(s,'get debug_mode').decode('utf-8','replace')[-300:])
cli(s,'exit',1)
time.sleep(.3)
rep,payload=enter_passthrough(s)
print('MSP_PASSTHROUGH_REPLY',rep.hex(' '), 'PAYLOAD', payload.hex(' ') if payload is not None else None)
if payload is not None:
    for cmd,name in [(0x31,'protocol_version')]:
        r,noise=send_4way(s,cmd,b'\x00',0,2)
        print(name, r, 'noise',noise.hex(' '))
    # Probe all four targets twice, leaving a clean exit between targets.
    for idx in range(4):
        for attempt in range(2):
            r,noise=send_4way(s,0x37,bytes([idx]),0,4)
            print('INIT target=%d attempt=%d'%(idx,attempt+1), r, 'noise',noise.hex(' '))
    exit_passthrough(s)
time.sleep(.5)
s.close()

# Query debug after one final target-specific pass (index 2 first, then controls).
for idx in [2,0,1,3,2]:
    s=serial.Serial(PORT,BAUD,timeout=.15,write_timeout=1)
    time.sleep(.2); enter_cli(s); cli(s,'exit',1); time.sleep(.2)
    rep,payload=enter_passthrough(s)
    print('\nTARGET',idx,'MSP',payload.hex(' ') if payload is not None else rep.hex(' '))
    if payload is not None:
        for a in range(3):
            r,noise=send_4way(s,0x37,bytes([idx]),0,4)
            print('  init',a+1,r)
        exit_passthrough(s)
    s.close(); time.sleep(.6)
    s=serial.Serial(PORT,BAUD,timeout=.15,write_timeout=1); enter_cli(s)
    out=cli(s,'showdebug')
    txt=out.decode('utf-8','replace')
    print('\n'.join(line for line in txt.splitlines() if 'debug' in line.lower() or 'ESC' in line or line.strip().startswith('#')))
    cli(s,'exit',1); s.close(); time.sleep(.4)
