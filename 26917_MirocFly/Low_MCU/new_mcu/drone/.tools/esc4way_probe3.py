import sys
import time
sys.path.insert(0, r'D:\RM\drone\.tools')
import esc4way_debug_probe2 as d

s, banner = d.enter_cli()
try:
    d.cli(s, 'set debug_mode = ESC', 2.0)
    d.cli(s, 'save', 1.0)
except Exception as exc:
    print('save/reboot exception (expected):', repr(exc))
try:
    s.close()
except Exception:
    pass
time.sleep(5.0)

s = d.wait_port(15.0)
print('OPEN_AFTER_REBOOT')
for esc_index in range(4):
    print(f'\n=== ESC target {esc_index} ===')
    frame, payload, prefix = d.enter_passthrough(s)
    print('MSP passthrough:', frame.hex(' '), 'payload=', None if payload is None else payload.hex(' '), 'prefix=', prefix.hex(' '))
    if payload is None:
        print('FAILED TO ENTER PASSTHROUGH')
        continue
    if esc_index == 0:
        response, noise = d.fourway(s, d.CMD_PROTOCOL_VERSION, b'\x00', 0, 3.0)
        print('protocol:', response, 'noise=', noise.hex(' '))
    response, noise = d.fourway(s, d.CMD_DEVICE_INIT_FLASH, bytes([esc_index]), 0, 5.0)
    print('initFlash:', response, 'noise=', noise.hex(' '))
    s.write(d.fourway_frame(d.CMD_INTERFACE_EXIT, b'\x00', 0)); s.flush()
    time.sleep(0.8)
    s.reset_input_buffer()
    s.write(d.msp_frame(d.MSP_DEBUG)); s.flush()
    dbg_frame, dbg_payload, dbg_prefix = d.read_msp(s, 4.0)
    dbg = d.decode_debug(dbg_payload)
    print('MSP_DEBUG:', dbg_frame.hex(' '), 'payload=', None if dbg_payload is None else dbg_payload.hex(' '), 'prefix=', dbg_prefix.hex(' '))
    if dbg is not None:
        print('debug[0..3]=', dbg[:4])
    time.sleep(0.5)
s.close()
print('\nDONE')
