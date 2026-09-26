# 2026-09-26_08_vision_control（实验）

**目的**：把实验 06（绿色检测）与实验 07（MSP 链路）**合成一个"识别绿色并飞过去"的程序**——
检测 → `offset/area` → `msp_offset_to_rc()` → `SET_RAW_RC` 注入。这就是阶段 1 的**视觉→控制闭环**雏形。

**结果**：✅ 程序编译并运行通过（PTY 假 FC：FC 端持续收到 `SET_RAW_RC`；无目标时正确回中）。
真机控制效果（符号/增益/油门）待下位机到位后标定。

## 数据流
```
VI 帧 → NV21→BGR → resize(640x360) → BGR2HSV → inRange → 最大轮廓
      → offset/area → msp_offset_to_rc(kp,max_delta) → SET_RAW_RC(AETR) → 串口/PTY
```

## 输出模式（两种）
- **无硬件（默认）**：内部开一个 **PTY**，后台线程模拟 FC，自动收帧并打印收到的摇杆 → 完整验证链路。
- **真硬件**：给串口设备则直接注入，如 `/dev/ttyS1 460800`。

## 用法
```
./green_fly [width] [height] [frames] [serial_dev] [baud]
```
- `frames=0` 一直跑。
- 例（离线自测）：`LD_LIBRARY_PATH=… ./green_fly 1280 720 150`
- 例（真 FC）：`LD_LIBRARY_PATH=… ./green_fly 1280 720 0 /dev/ttyS1 460800`

## 安全约定
- **未检测到目标 → 摇杆回中（1500）**。
- **油门默认 1000（不转）**，避免误飞；真机联调务必 **拔桨 → 绑绳 → 短飞**，先确认 `pitch/roll` 符号再给油。
- `pitch` 正负与 INAV ANGLE 约定需实机确认（当前 `roll=1500+kp·offx`、`pitch=1500-kp·offy`）。

## 文件
```
├── green_fly.cpp        # 主程序(检测+MSP 注入+PTY 假 FC)
├── camera.c/h           # 相机层(复制自实验04/06)
├── sophgo_middleware.h
├── msp.c/h              # MSP 链路(复制自实验07; 本副本 msp_serial_open 支持数值波特率)
├── Makefile             # opencv-mobile + board_libs + msp
└── README.md
```

## 已验证 / 待验证
- ✅ 编译链接、PTY 端到端、无目标回中、检测（在 06 已验证）。
- ⏳ 目标在画面时输出非中位修正摇杆（需绿物）；真 FC 联调（待下位机）。
