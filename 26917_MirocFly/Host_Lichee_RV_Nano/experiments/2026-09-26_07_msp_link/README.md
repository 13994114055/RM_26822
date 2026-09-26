# 2026-09-26_07_msp_link（实验）

**目的**：阶段 1 后半的链路层——实现 **MSP 客户端**（`SET_RAW_RC` 注入 + `RAW_IMU/ATTITUDE` 回读），
把视觉 `TargetInfo{offsetX,offsetY,area}` 映射为 RC 摇杆值，为「视觉→控制闭环」铺路。

**结果**：✅ 编解码/CRC 与端到端收发自测通过（无硬件，用 PTY 模拟 FC）。
**下位机未到**：真串口（UART1@460800）待新主控到位后联调。

## 目录
```
├── msp.h / msp.c     # MSP v1 编解码 + 流式解析 + 串口 + offset->RC 映射
├── msp_test.c        # 自测/工具 CLI
├── Makefile          # 纯 C, 只用工具链(不依赖中间件/板载库)
└── README.md
```

## API（`msp.h`）
- 编码/发送：`msp_v1_encode` / `msp_send_v1` / `msp_send_set_raw_rc` / `msp_send_request`
- 解析：`msp_parser_t` + `msp_parser_feed`（流式，逐字节喂）
- CRC：`msp_crc8_dvb_s2`（MSP v2 用）
- 串口：`msp_serial_open(dev,baud)` / `msp_serial_close`
- 映射：`msp_offset_to_rc(offx, offy, w, h, kp, max_delta, rc)` → AETR

MSP v1 帧：`$ M <dir> <len> <cmd> <data...> <csum>`，csum = XOR(len,cmd,data...)。

## 自测 / 运行（板上）
```bash
make
scp msp_test root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1 'cd /root/mirocfly && ./msp_test'      # selftest+pty+demo
# 或不带硬件跑单项:
./msp_test selftest
./msp_test pty          # PTY 模拟 FC: 发 SET_RAW_RC, 收 RAW_IMU
./msp_test demo         # 打印 offset->RC 映射
```
真硬件（FC 到位后）：
```bash
./msp_test monitor /dev/ttyS1 460800   # 被动解析 FC 发来的 MSP
./msp_test inject  /dev/ttyS1 460800 10 # 10Hz 注入 SET_RAW_RC(演示画圆)
```

## 关键点
- **无硬件可测**：用 **PTY（`posix_openpt`）在同一进程内模拟 FC**——
  客户端写 slave、模拟 FC 读 master，反之亦然；完整验证 termios + 分帧 + CRC，不依赖下位机。
- MSP 用 **CRC=异或**（v1），不是 CRC8；`crc8_dvb_s2` 是 v2 用。
- 8 通道 `SET_RAW_RC` 帧长 = 6 + 16 = **22** 字节（8×int16）。
- `offset->RC` 的 **pitch 正负与 INAV ANGLE 约定需实机确认**（当前 `roll=1500+kp·offx`、`pitch=1500-kp·offy`）。

## 下一步（闭环）
FC 到位后：`green_detect` 的 offset → `msp_offset_to_rc` → `msp_send_set_raw_rc`，10Hz 注入；拔桨→绑绳→短飞逐步验证。
