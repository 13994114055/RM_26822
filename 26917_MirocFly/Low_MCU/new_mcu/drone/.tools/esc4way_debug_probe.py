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


def xmodem(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def msp_frame(cmd: int, payload: bytes = b'') -> bytes:
    body = bytes([len(payload), cmd]) + payload
    checksum = 0
    for b in body:
        checksum ^= b
    return b'$M<' + body + bytes([checksum])


def fourway_frame(cmd: int, params: bytes = b'\x00', addr: int = 0) -> bytes:
    p = bytes(params)
    body = bytes([0x2F, cmd, (addr >> 8) & 0xFF, addr & 0xFF, len(p) & 0xFF]) + p
    crc = xmodem(body)
    return body + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def open_port(timeout=0.15):
    return serial.Serial(PORT, BAUD, timeout=timeout, write_timeout=1)


def read_until_prompt(s, timeout=5.0) -> bytes:
    end = time.time() + timeout
    out = bytearray()
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            out += chunk
            if out.rstrip().endswith(b'# '):
                break
    return bytes(out)


def enter_cli(s) -> bytes:
    s.reset_input_buffer()
    s.write(b'#\r\n')
    s.flush()
    time.sleep(0.15)
    return read_until_prompt(s, 4.0)


def cli(s, command: str, timeout=4.0) -> bytes:
    s.reset_input_buffer()
    s.write(command.encode('ascii') + b'\r\n')
    s.flush()
    return read_until_prompt(s, timeout)


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
            'cmd': frame[1],
            'addr': (frame[2] << 8) | frame[3],
            'len': size,
            'payload': payload,
            'ack': ack,
            'crc_ok': recv_crc == calc_crc,
            'crc': (recv_crc, calc_crc),
            'frame': frame,
        }, bytes(buf[:i])
    return None, bytes(buf)


def send_4way(s, cmd: int, params=b'\x00', addr=0, timeout=4.0):
    packet = fourway_frame(cmd, params, addr)
    s.write(packet)
    s.flush()
    return read_4way(s, timeout)


def open_after_reboot(timeout=12.0):
    end = time.time() + timeout
    last_error = None
    while time.time() < end:
        try:
            s = open_port(0.15)
            time.sleep(0.35)
            s.reset_input_buffer()
            s.write(b'#\r\n')
            s.flush()
            probe = read_until_prompt(s, 1.0)
            if probe.rstrip().endswith(b'# '):
                return s
            s.close()
        except Exception as exc:
            last_error = exc
        time.sleep(0.25)
    raise RuntimeError(f'COM4 did not recover: {last_error!r}')


def print_cli(text: bytes):
    decoded = text.decode('utf-8', 'replace').replace('\r', '')
    for line in decoded.splitlines():
        if line.strip():
            print('CLI>', line)


def parse_debug(payload):
    if payload is None:
        return None
    values = list(struct.unpack('<' + 'H' * (len(payload) // 2), payload[:len(payload) // 2 * 2]))
    return values


def main():
    s = open_after_reboot(5.0)
    print('OPEN_BEFORE_SETUP')
    print_cli(cli(s, 'status', 2.0))
    print_cli(cli(s, 'set debug_mode = ESC', 2.0))
    print_cli(cli(s, 'get debug_mode', 2.0))
    print_cli(cli(s, 'save', 2.0))
    s.close()
    time.sleep(0.5)
    s = open_after_reboot(12.0)
    print('OPEN_AFTER_REBOOT')

    for esc_index in range(4):
        print(f'\n=== ESC target {esc_index} ===')
        s.reset_input_buffer()
        s.write(msp_frame(MSP_SET_PASSTHROUGH))
        s.flush()
        frame, payload, prefix = read_msp(s, 4.0)
        print('MSP_SET_PASSTHROUGH:', frame.hex(' '), 'payload=', None if payload is None else payload.hex(' '), 'prefix=', prefix.hex(' '))
        if payload is None:
            print('passthrough failed')
            continue

        response, noise = send_4way(s, CMD_PROTOCOL_VERSION, b'\x00', 0, 3.0)
        print('protocol:', response, 'noise=', noise.hex(' '))

        response, noise = send_4way(s, CMD_DEVICE_INIT_FLASH, bytes([esc_index]), 0, 5.0)
        print('initFlash:', response, 'noise=', noise.hex(' '))

        # Exit the 4-way interface and wait for MSP to become available again.
        s.write(fourway_frame(CMD_INTERFACE_EXIT, b'\x00', 0))
        s.flush()
        time.sleep(0.8)
        s.reset_input_buffer()
        s.write(msp_frame(MSP_DEBUG))
        s.flush()
        dbg_frame, dbg_payload, dbg_prefix = read_msp(s, 4.0)
        values = parse_debug(dbg_payload)
        print('debug frame:', dbg_frame.hex(' '), 'payload=', None if dbg_payload is None else dbg_payload.hex(' '), 'prefix=', dbg_prefix.hex(' '))
        if values is not None:
            print('debug[0..1]=', values[:2] if len(values) >= 2 else values)
        # Wait until exits/reboot state settles before re-entering passthrough.
        time.sleep(0.5)

    s.close()
    print('\nDONE')


if __name__ == '__main__':
    main()
