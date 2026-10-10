# 2026-10-10_14_green_fly_smooth（实验）

**目的**：在实验 08「识别绿色并飞向」基础上，**打磨"视觉→运动"闭环的地基**：
1. **偏移 EMA 低通**（`alpha`）：平滑逐帧 offset，减少 RC 抖动；
2. **丢检滞回/保持**（`hold`）：单帧/短时漏检**不立刻回中**，保持并在 `hold` 帧内缓慢衰减，超时才回中；
3. **FC 遥测回读**：同一串口请求 `MSP_ATTITUDE`(108)/`MSP_RAW_IMU`(102)，得到 `roll/pitch/yaw`、`acc` → **真·双向闭环（下发 + 状态反馈）**；
4. 日志同时打印 `raw` 与平滑后的 `sm`，便于对比。

> 背景：08 在台架联调时，HSV 检测会 FOUND/no-green 交替 → RC 在"目标位"与"1500"间跳 → 真实飞行会抖。本实验解决这一层。

## 与 08 的差异
| 项 | 08 | 14（本实验） |
|---|---|---|
| offset | 逐帧直接映射 | **EMA 平滑**（`sx=α·x+(1-α)·sx`） |
| 漏检 | 立即回中(1500) | **保持+衰减**，超 `hold` 帧才回中 |
| FC 遥测 | 无（只下发） | **回读 `ATTITUDE/RAW_IMU`**（roll/pitch/yaw/acc），真双向 |
| baud 默认 | 460800（旧 AT32） | **230400**（G473） |

## 数据流
```
VI 帧 → NV21→BGR → resize(640×360) → BGR2HSV → inRange → 形态学
      → 最大轮廓 → offset/area
      → [EMA 平滑 + 丢检滞回] → msp_offset_to_rc(kp,max_delta) → SET_RAW_RC(AETR) → 串口/PTY
```

## 用法
```
./green_fly_smooth [w] [h] [frames] [serial_dev] [baud] [alpha] [hold]
```
- 省略 `serial_dev` → 内部 **PTY 假 FC**（无需硬件，可跑全链路）。
- 真硬件：`./green_fly_smooth 1280 720 0 /dev/ttyS0 230400`
- `alpha`：EMA 系数 0..1（默认 `0.35`，越小越平滑越滞后）。
- `hold`：丢检保持帧数（默认 `12`）。
- 环境变量 `MIROCFLY_ALPHA` / `MIROCFLY_HOLD` 可覆盖。

## 台架实测（2026-10-10，green_fly_smooth + 真 FC `/dev/ttyS0`）
```
seq=10 TRACK raw=(218,67) sm=(163,43) -> roll=1581 pitch=1479
seq=20 TRACK raw=(222,87) sm=(193,76) -> roll=1596 pitch=1462
seq=40 HOLD  raw=(0,0)   sm=(115,34) -> roll=1557 pitch=1483 (lost)   ← 漏检不回中
seq=50 TRACK raw=(190,57) sm=(200,71) -> roll=1600 pitch=1465
  [FC] att roll=-179.6 pitch=-4.1 yaw=22.9 acc=(37,-3,-511) rx_bytes=1260   ← 遥测回读(双向)
```
对比 08：同样场景下会 `no green -> neutral`（RC 抖到 1500）。

## 关键点 / 注意
- 相机/推流程序**同时只能跑一个**；跑本程序前先 `killall rtsp_grab frame_grab green_detect`。
- 退出用 **SIGTERM**（`killall green_fly_smooth`），别 `kill -9`（ION 泄漏）。
- 安全：目标真丢失 → 回中；油门固定 **1000（不转）**；真机联调 **拔桨→绑绳→短飞**。
- 控制**符号/增益**（`kp`、roll/pitch 方向）仍需**实机(ANGLE)验证**——台架只能看"检测→RC"链路。

## 文件
```
├── green_fly.cpp       # 主程序(检测+EMA平滑+丢检滞回+MSP/PTY)
├── camera.c/h          # 相机层(复制自 04/08)
├── sophgo_middleware.h
├── msp.c/h             # MSP 链路(复制自 07/08)
├── Makefile            # 目标 green_fly_smooth
└── README.md
```

## 后续
- 标定 `kp`、roll/pitch 符号（拔桨手持）；
- 基于遥测做撞击检测（`RAW_IMU` 加速度突增）；
- 任务状态机（起飞→搜索→接近）。
