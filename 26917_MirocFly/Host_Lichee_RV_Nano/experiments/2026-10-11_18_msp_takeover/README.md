# 2026-10-11_18_msp_takeover（实验）

**目的**：**用上位机操控下位机行动**——HSV 追踪绿色目标 → 生成 roll/pitch →
`MSP_SET_RAW_RC` 经 FC 的 **`MSP RC Override`** 模式接管 → 让飞机朝绿目标转/平飞过去。

> 与 14/17 的关系：14 是"检测→平滑→注入"的台架闭环，17 是"任务状态机干跑"。
> 18 聚焦**执行/接管这一件事**，并补齐两样关键：
> ① 明确的**接管语义 + 是否真接管的可观测**——**用 `MSP_RC`(105) 回读 FC 实际在用通道**与发送值对比（比 `MSP_ACTIVEBOXES` 可靠，见下）；
> ② **方向自检 `probe`**（脚本化发 roll±/pitch±，读 `MSP_MOTOR`/`MSP_ATTITUDE`/`MSP_RC`，按"相对 neutral 的 Δ"定符号/kp）。

> ⚠️ **`MSP_ACTIVEBOXES` 位序坑**：INAV `packBoxModeFlags()`（`fc_msp_box.c`）按**下发顺序的索引**打包（`bitArraySet(..., i)`，`i` = `activeBoxIds[]` 下标），**不是 box 枚举号**。所以 `bit41=BOXMSPRCOVERRIDE` 是错的（只有 `bit0=BOXARM` 恰好第 0 个可靠）。本实验改用 `MSP_RC`(105，返回 override **之后**的通道) 判接管：**我们发 roll=1700 时若 FCRC roll≈1700 → 接管生效**。

## ⚠️ 关键纠正：INAV `msp_override_channels` 位序 = AERT（不是 AETR）
逐行核对本固件 `inav-g4dbg/src/main/rx/msp_override.c` + `rx.c` + `fc/rc_controls.h`：
- INAV 内部通道枚举：`ROLL=0, PITCH=1, YAW=2, THROTTLE=3`（`rc_controls.h:25`）。
- 掩码按**内部通道号**逐位比较：`channelMask = 1<<channel; ... & channelMask`（`msp_override.c:269`）。
- 而 `SET_RAW_RC` 的**负载**顺序才是 AETR（`rcmap={0,1,3,2}`，`payload[2]=throttle`）。

| 掩码 | 二进制 | 实际接管 | 说明 |
|---|---|---|---|
| 15 | 1111 | roll/pitch/yaw/throttle | 全自主 |
| **11** | **1011** | roll/pitch/**throttle**（yaw 留给飞行员） | ❌ 会**用 payload 油门(1000)覆盖物理油门**，空中切油砸机 |
| **7** | **0111** | roll/pitch/yaw（throttle 留给飞行员） | ✅ **本实验采用**：只接管三轴，油门走物理 CRSF |

> Betaflight 的 `msp_override_channels_mask` 位序确实是 AETR（bit2=throttle）；**INAV 不同**。
> 这是很好的面试点：同一"RC Override"概念，两家位序不一致。

## 数据流（track 模式）
```
VI 帧 → NV21→BGR → resize(640×360) → BGR2HSV → inRange → 形态学
      → 最大轮廓 → offset(减 pitch_bias_px) → [EMA 平滑 + 丢检滞回]
      → msp_offset_to_rc(kp,max_delta) → SET_RAW_RC(AETR 负载, 16ch) → /dev/ttyS0 @230400
      → FC(MSP RC Override 模式 + msp_override_channels=7) 接管 roll/pitch/yaw
      → 回读 ATTITUDE / RAW_IMU / MOTOR / ACTIVEBOXES
```

## 用法
```
./takeover [mode] [w] [h] [frames] [serial_dev] [baud] [pitch_bias_px] [kp]
# mode = track | probe（默认 track）
# 省略 serial_dev → 内部 PTY 假 FC（无需硬件可跑全链路）
# 真 FC：
LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd \
  ./takeover track 1280 720 0 /dev/ttyS0 230400 0 0.5
# 台架方向自检（无需相机，压重物/绑绳）：
LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd \
  ./takeover probe 0 0 0 /dev/ttyS0 230400
```
- env：`MIROCFLY_KP` / `MIROCFLY_ALPHA` / `MIROCFLY_HOLD` / `MIROCFLY_SERIAL` / `MIROCFLY_BAUD`。

## 真机联调步骤（渐进）
1. **FC 侧**：`set msp_override_channels = 7` + `save`（已由本实验写入 `drone/config/g473_msprc.txt`）。
2. **台架 `probe`**（拔桨 **或** 压重物/绑绳）：
   - 飞行员：ARM + **ANGLE 模式**（ch6 低位，否则 roll/pitch 是角速度→自旋）+ **油门给到 `min_check`(1100) 以上（建议 ~1400）** + **ch8 抬起(>1700，全程保持)**。
   - 上位机跑 `probe` → 看日志：
     - **`OVERRIDE OK`**：接管生效（判据 = `MSP_RC` 回读的 roll/pitch 与我们发送值一致）；
     - 相位末 `>> PHASE ... dvs neutral=[...]`（相对 neutral 的 Δ）→ 定 **roll/pitch 符号**；调 `kp`。
     - FC mixer：`roll+` 期望 M1,M2 升；`pitch+` 期望 M1,M3 升。
3. **短飞 `track`**：绿目标进画面 → 飞机应朝目标**转/平飞**；不对就改 `msp_offset_to_rc` 的符号（一处一处改）。
4. 退出用 **SIGTERM**（`killall takeover`），别 `kill -9`（ION 泄漏）；相机程序互斥。

## 安全要点
- 油门位 payload 恒 **1000**；掩码=7 时被 FC 忽略，油门由飞行员 CRSF 控制。
- 目标真丢失 → 三轴回中(1500)；上位机停发 → FC 在 failsafe 延时内**回落到物理接收机**。
- 接管窗口由 **ch8** 决定（`aux 3 50 3 1700 2100`），飞行员随时可回收控制权（硬件兜底）。
- ⚠️ 接管时务必 **ANGLE**；ACRO 下 roll/pitch 是角速度，会被"持续旋转"。

## 待办 / 后续
- 用 `probe` 定死 roll/pitch 符号与 `kp`；相机换带 pitch 角模具后标 `pitch_bias_px`。
- 把本"接管执行块"接回 17 的任务状态机（真实油门/起飞；`track` 作为 APPROACH 的执行器）。
- 视觉改 YOLO（阶段 2）时，本层只需替换检测源，接管/链路不变。

## 文件
```
├── takeover.cpp     # 主程序: track(HSV→roll/pitch→SET_RAW_RC) + probe(方向自检)
├── camera.c/h       # 相机层(复制自 04/14/17)
├── sophgo_middleware.h
├── msp.c/h          # MSP(复制自 14/17; 本实验加 MSP_MOTOR/MSP_RC/MSP_ACTIVEBOXES)
├── Makefile         # 目标 takeover
└── README.md
```
