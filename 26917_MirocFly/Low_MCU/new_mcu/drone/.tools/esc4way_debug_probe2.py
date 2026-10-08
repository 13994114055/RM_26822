import sys
import time
import struct

sys.path.insert(0, r'D:\RM\drone\.tools\pyserial')
import serial

PORT = 'COM4'
BAUD = 115200
MSP_SET_PASSTHROUGH = 245
MSP_DEBUG = 254
CMD_PROTOCOL_VERSION = 0x31
CMD_INTERFACE_EXIT = 0x34
CMD_DEVICE_INIT_FLASH = 0x37


def xmodem(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def msp_frame(cmd, payload=b''):
    body = bytes([len(payload), cmd]) + payload
    checksum = 0
    for b in body:
        checksum ^= b
    return b'$M<' + body + bytes([checksum])


def fourway_frame(cmd, params=b'\x00', addr=0):
    p = bytes(params)
    body = bytes([0x2F, cmd, (addr >> 8) & 0xFF, addr & 0xFF, len(p) & 0xFF]) + p
    crc = xmodem(body)
    return body + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def open_port(timeout=0.2):
    return serial.Serial(PORT, BAUD, timeout=timeout, write_timeout=1)


def read_cli_prompt(s, timeout=4.0):
    end = time.time() + timeout
    out = bytearray()
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            out += chunk
            if out.endswith(b'# '):
                break
    return bytes(out)


def enter_cli(timeout=8.0):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            s = open_port()
            s.reset_input_buffer()
            s.write(b'#\r\n')
            s.flush()
            out = read_cli_prompt(s, 1.5)
            if out.endswith(b'# '):
                return s, out
            s.close()
        except Exception as exc:
            last = exc
        time.sleep(0.25)
    raise RuntimeError(f'CLI unavailable: {last!r}')


def cli(s, command, timeout=4.0):
    s.reset_input_buffer()
    s.write(command.encode('ascii') + b'\r\n')
    s.flush()
    return read_cli_prompt(s, timeout)


def wait_port(timeout=15.0):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            s = open_port()
            time.sleep(0.5)
            s.reset_input_buffer()
            return s
        except Exception as exc:
            last = exc
        time.sleep(0.2)
    raise RuntimeError(f'COM4 unavailable: {last!r}')


def read_msp(s, timeout=4.0):
    end = time.time() + timeout
    buf = bytearray()
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
        i = buf.find(b'$M>')
        if i < 0:
            continue
        if len(buf) < i + 6:
            continue
        size = buf[i + 3]
        if len(buf) < i + 6 + size:
            continue
        frame = bytes(buf[i:i + 6 + size])
        return frame, bytes(frame[5:5 + size]), bytes(buf[:i])
    return bytes(buf), None, bytes(buf)


def read_4way(s, timeout=4.0):
    end = time.time() + timeout
    buf = bytearray()
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
        i = buf.find(b'\x2e')
        if i < 0:
            continue
        if len(buf) < i + 5:
            continue
        size = buf[i + 4] or 256
        total = 8 + size
        if len(buf) < i + total:
            continue
        frame = bytes(buf[i:i + total])
        payload = frame[5:5 + size]
        ack = frame[5 + size]
        recv_crc = (frame[6 + size] << 8) | frame[7 + size]
        calc_crc = xmodem(frame[:6 + size])
        return {
            'cmd': frame[1], 'addr': (frame[2] << 8) | frame[3], 'len': size,
            'payload': payload, 'ack': ack, 'crc_ok': recv_crc == calc_crc,
            'crc': (recv_crc, calc_crc), 'frame': frame,
        }, bytes(buf[:i])
    return None, bytes(buf)


def fourway(s, cmd, params=b'\x00', addr=0, timeout=4.0):
    s.write(fourway_frame(cmd, params, addr))
    s.flush()
    return read_4way(s, timeout)


def enter_passthrough(s, retry=True):
    for attempt in range(2 if retry else 1):
        s.reset_input_buffer()
        s.write(msp_frame(MSP_SET_PASSTHROUGH))
        s.flush()
        frame, payload, prefix = read_msp(s, 3.0)
        if payload is not None:
            return frame, payload, prefix
        if attempt == 0:
            time.sleep(0.7)
    return frame, payload, prefix


def decode_debug(payload):
    if payload is None:
        return None
    count = len(payload) // 2
    return list(struct.unpack('<' + 'H' * count, payload[:count * 2]))


def show_cli(label, text):
    print(f'--- {label} ---')
    for line in text.decode('utf-8', 'replace').replace('\r', '').splitlines():
        if line.strip():
            print(line)


def main():
    s, banner = enter_cli()
    show_cli('CLI', banner)
    show_cli('set debug_mode', cli(s, 'set debug_mode = ESC'))
    show_cli('get debug_mode', cli(s, 'get debug_mode'))
    show_cli('resource MOTOR', cli(s, 'resource MOTOR 1'))
    show_cli('save', cli(s, 'save', 3.0))
    s.close()
    time.sleep(4.0)

    s = wait_port()
    print('OPEN_AFTER_REBOOT')

    for esc_index in range(4):
        print(f'\n=== ESC target {esc_index} ===')
        frame, payload, prefix = enter_passthrough(s)
        print('MSP passthrough:', frame.hex(' '), 'payload=', None if payload is None else payload.hex(' '), 'prefix=', prefix.hex(' '))
        if payload is None:
            print('FAILED TO ENTER PASSTHROUGH')
            continue

        if esc_index == 0:
            response, noise = fourway(s, CMD_PROTOCOL_VERSION, b'\x00', 0, 3.0)
            print('protocol:', response, 'noise=', noise.hex(' '))

        response, noise = fourway(s, CMD_DEVICE_INIT_FLASH, bytes([esc_index]), 0, 5.0)
        print('initFlash:', response, 'noise=', noise.hex(' '))

        s.write(fourway_frame(CMD_INTERFACE_EXIT, b'\x00', 0))
        s.flush()
        time.sleep(0.7)

        s.write(msp_frame(MSP_DEBUG))
        s.flush()
        dbg_frame, dbg_payload, dbg_prefix = read_msp(s, 3.0)
        dbg = decode_debug(dbg_payload)
        print('MSP_DEBUG:', dbg_frame.hex(' '), 'payload=', None if dbg_payload is None else dbg_payload.hex(' '), 'prefix=', dbg_prefix.hex(' '))
        if dbg is not None:
            print('debug[0..3]=', dbg[:4])
        time.sleep(0.4)

    s.close()
    print('\nDONE')


if __name__ == '__main__':
    main()
