# Lyi_F722 — 新飞控开发日志（当前阶段）

> **状态**：🟡 起步（2026-11-04 建目录，板卡信息待补）
> **取代**：`../BateFPV_G473/`（已冻结，只读参考）
> **上位机**：`../../Host_Lichee_RV_Nano/`（LicheeRV Nano）

---

## 1. 为什么换板（决策背景）

| 旧板 G473（冻结） | 新板 F722 |
|---|---|
| STM32G474，512KB Flash | STM32F722（**Flash 待确认**，F722 常见 512KB） |
| **无气压计** → 需自研"测距当 Z 源"的 Z 分支 | **有气压计** → Z 轴可用标准固件 |
| 定高依赖自研固件分支（维护面大） | 目标：回归标准 INAV / 其他 |
| 定点已退役（光流 2–3cm/s 原理性漂移） | 不再做世界定点 |

**关键动机**：起点是 1.2m 高台，飞离后 AGL 从 0.3m 突跳 1.5m——**AGL 式定高会误判并下坠 1.2m**。气压计给绝对高度基准，可从根上解决。

---

## 2. 板卡信息（待填）

| 项 | 值 | 备注 |
|---|---|---|
| 型号 | Lyi F722 | |
| MCU | STM32F722 ? | 确认 Flash/RAM |
| IMU | ? | 型号 + 总线（SPI/I2C） |
| **气压计** | ? | 型号（BMP280/DPS310…）+ 总线 |
| ESC | ? | 四合一 / DShot 协议 |
| UART 数量/映射 | ? | 至少需要：**MSP（上位机）**、CRSF（接收机）、光流（可选） |
| 电压采样 | ? | `vbat_scale` 待标定 |
| 固件 | **INAV** | 2026-11-04 定：F722 = 512KB Flash，PX4 需 ≥1MB → **PX4 出局**；INAV 可复用上位机 MSP 全套代码 |

> 填法：USB 接上后 `version` / `status` / `dump` 读回，把结果贴到下面"3. 首次上电记录"。

### 2.1 固件选型结论（2026-11-04）

**选 INAV**，理由按硬门槛排序：

| 判据 | 结论 |
|---|---|
| **Flash** | F722 = **512KB**；PX4 需要 ≥1MB（FMUv5/6 都是 2MB）→ **PX4 装不下** |
| **上位机代码复用** | 现有 `SET_RAW_RC` + `ACTIVEBOXES` 门控 + exp22/23/24 全部基于 MSP，INAV 零改动 |
| **气压计** | INAV 有 baro 后 ALTHOLD 原生可用，**不再需要自研 Z 分支** |
| **风险** | 延续既有经验，学习成本最低 |

### 2.2 关键前提：找 target（引脚映射）

**board target = 引脚映射表**（哪个 UART 是哪个脚、IMU 在哪个 SPI、LED/蜂鸣器/电压采样脚）。同名 MCU 不同板子，引脚完全不同。

本地 INAV 源码树（`../BateFPV_G473/drone/inav/`）有 **261 个 target**，含大量 F722 板：
`BETAFPVF722`、`GEPRCF722`、`HGLRCF722`、`IFLIGHT_BLITZ_F722`、`FLYWOOF722PRO` …

**但没有 `LYI`。** 所以按下面顺序找：

1. **读板子现有固件的 `version`** —— 若板子出厂是 Betaflight，版本串里会带 **BF target 名**（如 `MATEKF722SE`）。这是判断"它是谁的克隆"的最快线索；
2. 用该 target 名在 INAV 树里找同名或同 MCU 的 target；
3. 对比**引脚定义**（`target.c`/`config.h` 里的 `DEFIO`/UART 映射）与板子实际丝印；
4. 三者都对得上 → 直接编译该 target；
5. 对不上 → **复制最接近的 target 改引脚**（工作量 0.5–2 天，需原理图或万用表实测）。

> ⚠️ 使用"相近 target"前必须在台架上验证：**电机序号/转向、UART 是否通、电压采样是否正确**——引脚错一位就可能烧 ESC 或读不到电压。

---

## 3. 首次上电记录（待填）

```
# 读取命令（USB VCP）
version
status
dump
mmix
```

| 检查项 | 结果 |
|---|---|
| 固件版本 | |
| GYRO/ACC/BARO 状态 | |
| UART 映射 | |
| 电压读数 | |
| 电机/混控 | |

---

## 4. 接口规划（草案，待确认）

```
上位机 LicheeRV ──UART(MSP, 230400)──> FC
遥控器 ELRS ────UART(CRSF)───────────> FC
光流 MTF-02P ───UART(MSP)────────────> FC   （可选，做速度阻尼）
VIO MicoVIO ────UART(MAVLink,921600)─> 上位机（不走 FC）
```

| 用途 | FC 口 | 参数 | 状态 |
|---|---|---|---|
| 上位机 MSP | ? | 230400 | 待定 |
| 物理 RC | ? | CRSF | 待定 |
| 光流 | ? | MSP | 待定 |
| 黑匣子 | ? | — | 待定 |

---

## 5. 关键设计约束（沿用上位机阶段结论）

1. **高度基准必须是绝对的**（气压计/VIO），AGL 只做辅助与接地判断；
2. **姿态环永远在 FC**，上位机只管 XY（+ 后期油门）；
3. **权限门**：`mask=7`（三轴）→ 后续 `mask=15`（含油门）；**ARM 通道不可覆盖，解锁永远物理 ch5**；
4. **模式开关**（ANGLE/ALTHOLD）上电前摆好，飞行中不动；
5. 相机 12° 定稿，攻击高度 = 目标 +10cm，commit 0.4m，末速 ≤0.7m/s。

---

## 6. 日志索引

> 每次调试新建 `logs/YYYY-MM-DD_主题.md`，并在下表登记。

| 日期 | 主题 | 结论/状态 |
|---|---|---|
| 2026-11-04 | 建目录、板卡信息占位 | 🟡 待首次上电 |

---

## 7. 待办（按顺序）

- [ ] **读 `version`**：确认板载固件 target 名（判断它是谁的克隆）← 现在最该做的一步
- [ ] 上电读 `status`/`dump`/`mmix`，填 §2 §3
- [ ] 确认气压计可用（`status` 里 BARO 状态）
- [ ] **在 INAV 源码树里匹配 target**（§2.2 五步法）
- [ ] 编译 + 刷入（`.hex`，保留配置区；刷前确认 bootloader/DFU 可用）
- [ ] 配 UART：MSP 给上位机、CRSF 给接收机、光流（可选）
- [ ] 拔桨验传感器 + **电机序号/转向** + 电压采样
- [ ] 复刻旧板开关语义：`ch5`=ARM、`ch6/ch8`=MSP OVERRIDE、`ch7` 中=ALTHOLD、mask=7
- [ ] 定高：气压计为 Z 源，验证"跨台阶不追地面"
- [ ] 上下位机 MSP 链路复测（复用 `BateFPV_G473/connection/fccli.py` 与 gate 逻辑）

## 8. 开关矩阵与 INAV aux 配置（2026-11-05 定稿）

### 8.1 矩阵（用户定）

| 开关 | 何时有效 | 作用 |
|---|---|---|
| **ch5** | 永远 | ARM（解锁/上锁，唯一人工急停） |
| **ch8 > 1700** | 永远 | MSP RC OVERRIDE（上位机接管门） |
| **ch6** | 仅接管时 | 上位机子模式：<1300 空闲 / 1300–1700 yaw 跟随 / >1700 预留 |
| **ch7** | 接管关闭时 | FC 自己：1300–1700 定高 / 1700–2100 三轴定点 |

> ⚠️ **纪律**：ch7 在 1700–2100（POSHOLD）时**不要**同时把 ch8 拨到 >1700 接管——两者会同时闭环 XY。

### 8.2 INAV CLI aux 配置（对应上表）

```
aux 0 0 0 1775 2100      # ch5  1775-2100 = ARM
aux 1 50 3 1700 2100     # ch8  1700-2100 = MSP RC OVERRIDE（接管）
aux 2 1 3 900 2100       # ch8  900-2100  = ANGLE（角度模式常开）
aux 3 3 2 1300 2100      # ch7 1300-2100  = NAV ALTHOLD（定高）
aux 4 11 2 1700 2100     # ch7 1700-2100  = NAV POSHOLD（三轴定点）
msp_override_channels = 7    # 只接管三轴；将来油门接管改 15
```

> 说明：`aux` 第二列是**永久 ID**（ARM=0、MSP RC OVERRIDE=50、ANGLE=1、ALTHOLD=3、POSHOLD=11）；第三列是 **aux 通道号**（0→ch5，1→ch6，2→ch7，3→ch8）。

### 8.3 上位机侧对应行为

- 读 `MSP_RC(105)` 的 rc[4..7] 得到 ch5–ch8；
- 读 `MSP_ALTITUDE(109)` 判断是否离地；
- 接管（ch8>1700）后：ch6<1300 只稳、1300–1700 yaw 跟随 + 俯仰接近；
- 未接管时上位机完全不介入（发中性）。

## 9. 串口与 MSP 现状（2026-11-09 实测）

### 9.1 飞控侧串口映射（INAV `serial` 命令读回）

```
serial 20 1 115200     # USB VCP → MSP
serial 0  1 115200     # UART0 → MSP
serial 1  0 115200     # 未启用
serial 2  1 230400     # ★ UART2 → MSP @230400（板子丝印「串口3」→ 接上位机）
serial 3  64 115200    # UART3 → RX_SERIAL（板子丝印「串口4」→ 接数字接收机）
serial 4  0 / serial 5 0

receiver_type = SERIAL
feature = VBAT TX_PROF_SEL TELEMETRY CURRENT_METER BLACKBOX AIRMODE PWM_OUTPUT_ENABLE OSD
```

功能码对照（`io/serial.h`）：`1`=MSP、`2`=GPS、`64`=RX_SERIAL、`128`=BLACKBOX、`256`=MAVLINK。

> ⚠️ **板子丝印 = INAV 索引 + 1**：丝印「串口3」= INAV `serial 2`（MSP，接上位机）；丝印「串口4」= INAV `serial 3`（RX_SERIAL，接数字接收机）。

### 9.2 MSP 速度上限

飞控 MSP 口实测最高 **230400**。上位机若用 115200 → **完全静默**（不是乱码报错，是一个字节都收不到）。

### 9.3 box 映射（授权门）

| bit | pid | 含义 |
|---|---|---|
| 0 | 0 | ARM |
| 3 | 1 | ANGLE |
| 12 | 3 | NAV ALTHOLD |
| 17 | 27 | AUTOTRIM（**意外激活，待排查**） |
| **20** | **41** | **MSP RC OVERRIDE（授权门）** |
| 24 | 50 | CHANGEMISSION |

源码：`src/main/fc/rc_modes.h` → `BOXMSPRCOVERRIDE = 41`。
**旧板"pid50=OVERRIDE"是误判。**

### 9.4 台架模式（无接收机时验证链路）

```
set receiver_type = MSP
save
```

上位机发 ch5/ch6/ch8 即等同物理 RC，可验证解锁/模式/授权门。
**飞行前必须 `set receiver_type = SERIAL` 并 `save`。**
