# 2026-10-12_19_opflow_hold（实验）

**目的（方法 O2）**：**上位机侧"光流水平阻尼/定点"**——读下位机 FC 的光流+测距，估计水平速度，在 roll/pitch 上加"速度阻尼"，抑制**水平漂移**（进而减轻因持续侧倾导致的**掉高度**）。

> 背景：黑匣子复盘（工具 `tools/blackbox_decode.py`）证实——段13 接管（`override=1` @9.70s）后，机身持续左倾/低头、**测距高度 1m→0**、而油门恒定（飞行员没补）→ 掉高度；根因是**无位置保持**（ANGLE 只稳姿态）+ 倾斜损失升力。
> O2 用光流压住水平速度 → 少侧倾 → 少掉高度。

## 数据通路（已确认可用）
| 命令 | 类型 | 返回 |
|---|---|---|
| `MSP2_INAV_OPTICAL_FLOW` (0x2001) | MSP **v2** | `quality(u8)` + `flowRateX/Y(i16)` + `bodyRateX/Y(i16)` 度/s |
| `MSP_SONAR_ALTITUDE` (58) | MSP v1 | 测距高度 `u32` [cm] |
| `MSP_ATTITUDE` (108) / `MSP_RC` (105) | v1 | 姿态 / FC 实际通道 |

- **平移流 = flowRate − bodyRate**（去掉机身角速度带来的流）；水平速度 ≈ 平移流(rad/s) × 高度(m)。
- 因此 `msp.c/h` **新增 MSP v2 发送 + 统一解析器**（`msp2_send_request` / `msp_parser2_feed`，同时吃 v1 `$M` 与 v2 `$X` 帧，v2 CRC=crc8_dvb_s2）。

## 模式
- `mon`：**只读**光流/测距/姿态/RC，打印；**不发任何控制**（先验证 MTF-02P 数据与轴/符号）。
- `damp`：读光流估水平速度 v，下发 `roll/pitch = 1500 − kd·v`（**符号/轴待台架标定**）。（**待实现**）

## 用法
```
./flow_damp [mode=mon] [serial_dev] [baud=230400]
# 省 serial_dev -> PTY 假 FC
LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./flow_damp mon /dev/ttyS0 230400
```
env：`MIROCFLY_KD`（阻尼增益）、`MIROCFLY_ALT_M`（无测距兜底高度）、`MIROCFLY_SECS`（限时）。

## 步骤（渐进）
1. `mon` 静止 → 看 `q`（quality>0）、`flowX/flowY` 噪声；**水平平移机身** → 看 `transX/transY` 的符号/量级（定轴与符号）。
2. 实现 `damp`，台架（压重物）验证阻尼方向。
3. 真机：`damp` 抑制漂移；再叠加视觉（把 exp18 的检测接进来做"朝目标飞"）。

## 文件
```
├── flow_damp.cpp     # mon（只读验证）+ damp（待实现）
├── msp.c/h           # MSP v1 + 新增 MSP v2
├── camera.c/h        # 相机层（后续视觉用）
├── sophgo_middleware.h
├── Makefile
└── README.md
```
