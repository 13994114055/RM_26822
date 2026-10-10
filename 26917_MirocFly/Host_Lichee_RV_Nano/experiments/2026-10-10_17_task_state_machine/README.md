# 2026-10-10_17_task_state_machine（实验）

**目的**：把前面各块**整合成任务状态机雏形**，跑通"感知→决策→下发→下位机状态"的**任务流程**。
**干跑**：油门固定 **1000（不转）**，只跑逻辑 + 日志 → **无电池即可台架验证**。

## 状态机（阶段1 简化版）
```
IDLE --(auto-arm, dry)--> ARMED --(takeoff cmd)--> TAKEOFF --(climb done, dry)--> SEEK
SEEK --(有目标)--> APPROACH
APPROACH --(撞击: IMU突增 或 目标面积≥阈值)--> IMPACT --(1s)--> RECOVER --(1s)--> RTH --(8s)--> LAND --(1s)--> DONE
APPROACH --(丢目标超 3s)--> SEEK
```
- **输入**：视觉 `{found, sx, sy, area}`（EMA 平滑 + 丢检滞回）、FC `{ATTITUDE, RAW_IMU}`、计时。
- **输出**：各状态 RC（干跑油门 1000）；状态迁移日志（阶段/原因/offset/area/RC/姿态）。
- **撞击判据**（二选一，取或）：
  - **IMU**：`MSP_RAW_IMU` 加速度模长**相对基线**突增（见 `experiments/16`）；
  - **视觉**：目标面积≥`area_impact`（默认 1000，检测分辨率 640×360 上的 contourArea）。
- **相机俯仰偏置**：`pitch_bias_px`（默认 0）——只作用于**垂直回中基准** `eff_y = sy − pitch_bias_px`，yaw/roll 不动。**换带 pitch 角模具时只改它 + 单点标定**（目标放机头正前方读 `sy`）。

## 用法
```
./task_sm [w] [h] [frames] [serial_dev] [baud] [pitch_bias_px] [area_impact]
# 真 FC:
LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd \
  ./task_sm 1280 720 0 /dev/ttyS0 230400 0 1000
# 省略 serial_dev -> PTY 假 FC(纯逻辑)
```

## 台架实测（2026-10-10，真 FC）
把绿目标怼近镜头（面积 2807→1796）触发视觉撞击判据，**整链跑通**：
```
[t= 2.04] >>> ARMED    | auto-arm(dry)
[t= 4.04] >>> TAKEOFF  | takeoff cmd
[t= 6.05] >>> SEEK     | climb done(dry)      area=2807
[t= 6.18] >>> APPROACH | target found
[t= 6.30] >>> IMPACT   | IMPACT! (area>=thr)
[t= 7.42] >>> RECOVER  | impact hold done
[t= 8.43] >>> RTH      | recovered -> RTH
[t=16.43] >>> LAND     | home reached(dry)
[t=17.56] >>> DONE     | landed
```
- 第 1 轮（`area_impact` 未稳）也见过 `SEEK→APPROACH` 稳定跟踪：`off=(94,-146) RC=(1547,1573)`。

## 注意 / 待办
- **干跑**：TAKEOFF/RTH 未实际给油/移动（占位计时）；真机需接油门/位置。
- **IMU 撞击未实测触发**：轻敲似不足以让 FC 回读的 `acc` 突增（疑 `MSP_RAW_IMU` 加速度被滤波）→ 先用**视觉面积**判据演示；后续可换更灵敏的 IMU 源或降阈值。
- `kp`/roll-pitch 符号仍待实机标定；相机 pitch 偏置待换模具后标定。
- 相机程序互斥；退出 SIGTERM（ION）。

## 文件
```
├── task_sm.cpp      # 状态机 + 检测/平滑 + MSP/遥测 + 撞击判据 + pitch偏置
├── camera.c/h       # 相机层(复制自 04/14)
├── sophgo_middleware.h
├── msp.c/h          # MSP(数值波特率版)
├── Makefile         # 目标 task_sm
└── README.md
```
