# agent_at32 — AT32F435 下位机开发经验全集（⚠️ 已冻结，不再更新）

> **本文件是什么**：AT32F435 下位机（含板子、外设、INAV 阶段、自写固件路线、硬件教训）的**完整开发经验存档**。
> **状态**：⚠️ **已冻结（停止更新）**。因下位机方案更替（见第 0 节），AT32 方案被淘汰，本文档仅作历史经验/面试复习之用。
> **上位机**的持续开发请看 `Host_Lichee_RV_Nano/AGENT_GUIDE.md` 与根目录 `README.md` / `agent.md`。

---

## 0. 为什么冻结（方案更替）

- **硬件瓶颈**：AT32F435 方案只能**直驱空心杯电机**，实测**升力仅约 59g**，而**整机重约 76g** → **无法起飞**。
- **决策**：更换下位机与动力系统——
  - **电机**：空心杯 → **无刷电机**
  - **主控**：换成**自带无刷驱动（ESC）的主控**
  - **保留**：上下位机**接口/通信**部分（MSP / 自定义帧语义不变）
- **当前**：新主控硬件**尚未到手**，其开发暂缓；AT32 的这段开发经验全部留档于此。

---

## 1. 项目总体方向（双重目标）

- **技术目标**：自主撞击不规则物体，30 秒内返航。
- **就业目标**：展示「控制 + Linux + 边缘AI部署」复合能力。
- **下位机角色（原 AT32 定位）**：近期作为 **INAV 黑盒稳定器**（只配置，不改固件）；远期实现**自研飞控**（姿态解算 + PID + 状态机 + 撞击检测 + 返航），体现"控制"能力。

## 2. 系统架构（两阶段演进）

```
LicheeRV Nano (SG2002) 上位机 —— 视觉/决策
        │  近期: MSP (SET_RAW_RC 注入 + RAW_IMU/ATTITUDE 回读)
        │  远期: 自定义帧 0xAA 0x55 | X:int16 | Y:int16 | Area:uint16 | Status:uint8 | CRC8
        ▼
AT32F435（原下位机）—— 近期: INAV(黑盒稳定器, 预编译不可重编)
        │                  远期: 自写固件(姿态解算+PID+混控+状态机+撞击检测+30s返航)
        ├── UART1 MSP(上位机)   ├── UART7 CRSF 接收机(物理RC直连, RC优先)
        ├── UART5 MTF-02P 光流   ├── SPI1 LSM6DSOWTR IMU
        └── I2C2 QMC5883P 磁力计 + SPL06-001 气压计   └── TMR2 4通道 PWM(电机)
```

**接口抽象**：上位机统一产出 `TargetInfo{偏移X, 偏移Y, Area(∝1/距离²), Status}`，链路层仅换编码器
（近期 MSP 摇杆值 / 远期自定义帧），上层任务逻辑两阶段零改动。**这一接口约定在新主控上继续沿用。**

## 3. 关键决策记录

| 决策 | 内容 |
|---|---|
| 下位机策略 | 近期 INAV（只配置不改固件）；远期自研飞控（原计划，现因动力方案更替暂搁置） |
| 通信协议 | 近期 **MSP**（INAV 串口仅支持 MSP，不支持 MAVLink）；远期自定义帧 `0xAA 0x55` |
| INAV 固件 | 预编译，**不能重新编译**（无法加 `USE_MSP_RC_OVERRIDE` 开关） |
| RC 优先 | INAV `MSP RC Override` 飞行模式（需 INAV ≥8.1）+ `msp_override_channels`=AETR；飞行员拨杆切换 |
| 撞击检测 | 近期上位机轮询 `MSP_RAW_IMU`(102) 加速度突增(>4–6g) + 视觉 bbox 达阈值；远期下位机 IMU 检测 |
| 返航 | 无 GPS：指令死推算为主 + `MSP2_INAV_ESTIMATED_POSITION` 光流位置辅助 |
| 任务预算 | 30s：接近~8s / 撞击自稳~5s / 返航~15s |
| 电机/主控 | **（被淘汰）** AT32 直驱空心杯升力不足 → 改无刷 + 自带 BLDC 驱动的主控 |

## 4. 硬件规格与引脚（AT32F435）

- **主控**：AT32F435CGU7（QFN48，Cortex-M4），**288MHz 超频**（超标准主频，稳定性待验证），1MB Flash / 128KB SRAM
- **关键外设/引脚**（`Low_MCU/AT32F435CGU7_project/`）：
  - **UART1** = 上位机（MSP），PA9/PA10，**460800**
  - **UART5** = 光流计 MTF-02P，PB8/PB9，**115200**
  - **UART7** = CRSF 数字接收机（RC 源），PC7/PC8，**4800**
  - **SPI1** = IMU（LSM6DSOWTR），PA5/PA6/PA7/PA4（SCK/MISO/MOSI/CS），全双工
  - **I2C2** = 磁力计 QMC5883P(0x2C) + 气压计 SPL06-001(0x77)，PB10/PB11
  - **TMR1** = 时间基准；**TMR2 = 4 通道 PWM**（电机输出，ARR=14399）：
    - CH1/PA15、CH2/PB9、CH3/PA2、CH4/PA3
  - **DMA1** 多通道：UART1/5/7 收发
- **板子手册**：`Low_MCU/下位机MCU手册.pdf/.docx`（内容较浅，仅 INAV 程序使用说明）

## 5. AT32 工程现状（Workbench 骨架，非可飞固件）

工程位置：`Low_MCU/AT32F435CGU7_project/`

**已初始化**（`wk_*.c`）：
- `wk_usart.c`：UART1(460800)/UART5(115200)/UART7(CRSF)
- `wk_spi.c`：SPI1（IMU）；`wk_i2c.c`：I2C2（磁力计/气压计）
- `wk_tmr.c`：TMR1（时间基准）、**TMR2 4通道PWM**（电机）
- `wk_dma.c`：DMA1 多通道
- FreeRTOS：`freertos_app.c` 内 **9 个空任务**骨架：`pid/imu/i2c/hitdetect/vision/flow/rc/count30s/sm`

**实际状态**：仅 `vision_task` 内有一段 **UART1 回环测试 + 单通道电机 PWM 示例**。**无 IMU 读取/姿态解算/PID/MSP 解析/CRSF 解析/光流解析/撞击检测/状态机**——**不可直接用于飞行**。

## 6. INAV 阶段（已刷入，只配置不改固件）

- **板上 INAV 版本**：**INAV 9.0.0**（target `HUMPBACK_LIGHTFIN`，构建 2026-02-10，GCC 13.2.1）。
  - 9.0.0 ≥ 8.1 → **`MSP RC Override` 默认启用**，`msp_override_channels` 掩码为 32 位。
- **配置清单**（CLI/Configurator）：
  - UART1 = MSP（上位机，460800）；UART7 = CRSF 接收机
  - `msp_override_channels` = AETR 掩码（如 15 = 通道1-4）
  - 遥控器把 `MSP RC Override` 映射到一个拨杆（自主/手动切换）
  - **关闭 `crash_detection_*`**（避免与任务撞击逻辑冲突）
  - 配置 fail-safe（上位机停发 → 回落物理 RC → 安全动作）
- **联携方式**：上位机 `MSP_SET_RAW_RC`（≥5Hz，推荐 10Hz）注入摇杆；停发自动回落物理 RC。
- **验证**：拔桨 → 绑绳测试拨杆切换与 fail-safe。

## 7. 自写固件路线（远期，原计划，现搁置）

在 Workbench 骨架上按依赖顺序实现，每步拔桨→绑绳→短飞验证：

| 步骤 | 任务 | 说明 |
|---|---|---|
| 3.1 | IMU + 姿态 | SPI1 读 LSM6DSOWTR @1kHz + 零偏校准 + Mahony 姿态解算 |
| 3.2 | PID + 混控 | 1kHz 角速率/角度 PID + X 混控 → TMR2 四通道输出 |
| 3.3 | CRSF + Override | UART7 CRSF 解析 + 解锁 + 复刻 INAV Override 语义 |
| 3.4 | 光流 + 位置 | UART5 MTF-02P 解析 + 位置积分（返航用） |
| 3.5 | 自定义帧 | UART1 解析 `AA 55 | X | Y | Area | Status | CRC8` + 回读遥测 |
| 3.6 | 撞击 + 状态机 | IMU 突增(>4–6g)或电机掉速判撞击 → 自稳 1s → 状态机 |
| 3.7 | 集成 | 与上位机自定义帧编码器联调全任务 |

## 8. 通信协议（上↔下，**新主控继续沿用**）

- **协议**：近期 MSP（`MSP_SET_RAW_RC`=200 注入；回读 `MSP_RAW_IMU`=102、`MSP_ATTITUDE`=108、`MSP_RX_MAP`、`MSP2_INAV_STATUS`、`MSP2_INAV_ESTIMATED_POSITION`）。
- **自定义帧（远期）**：`AA 55 | X:int16 | Y:int16 | Area:uint16 | Status:uint8 | CRC8`。
- **RC 优先**：物理 CRSF 直连为准，拨杆切换自主/手动（INAV 用 `MSP RC Override`；自写固件复刻同一语义）。

## 9. 硬件教训（核心，务必记录）

1. **动力/升力必须匹配整机重量**：AT32 直驱**空心杯**实测升力约 **59g**，整机 **76g** → **飞不起来**。
   → 选型时**先核算总升力 ≥ 1.5~2× 整机重量**，再定电机与驱动。
2. **空心杯直驱 vs 无刷+ESC**：空心杯直驱简单但推力密度低；无刷推力大，需电调/自带驱动的主控。
3. 新方案：**无刷电机 + 自带无刷驱动的主控**；**上下位机接口保持不变**。

## 10. 风险与遗留（原 AT32 侧）

- 288MHz 超频稳定性（原计划 216MHz 起步验证）。
- MTF-02P 光流协议与 INAV 兼容性未知 → INAV 阶段不依赖，自写固件自研解析。
- 撞击后加速度计可能饱和，检测窗口要短且只触发一次。
- INAV 原生 `crash_detection_*` 需关闭。

## 11. 参考资料

- 工程：`Low_MCU/AT32F435CGU7_project/`（AT32 Workbench 生成）
- 板子手册：`Low_MCU/下位机MCU手册.pdf` / `.docx`
- INAV 文档：`MSP_SET_RAW_RC` / `MSP RC Override` / `msp_override_channels`
- 上位机侧对接：`Host_Lichee_RV_Nano/AGENT_GUIDE.md`、根 `README.md` / `agent.md`
- 困难与解决：`Host_Lichee_RV_Nano/difficulty_and_method.md`

---

## 更新日志
- **2026-09-26**：由 `AGENT_GUIDE.md` 更名并扩充为 AT32 完整经验存档；记录动力不足（59g<76g）导致方案更替（无刷+新主控）；**本文件自此冻结**。
