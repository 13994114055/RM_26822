# MirocFly 下位机（Low_MCU）

> 本目录是 MirocFly 的**下位机（飞行控制器，执行器）**开发区。
> 上位机（LicheeRV Nano，视觉/决策）在 `../Host_Lichee_RV_Nano/`。

## 当前状态

- **原 AT32F435 方案已淘汰**：直驱空心杯实测升力约 59g < 整机 76g，**无法起飞**。
- **新方案**：**无刷电机 + 自带无刷驱动（ESC）的主控**；**硬件尚未到手**，相关开发暂缓。
- **上下位机接口/通信约定保持不变**（详见下），所以上位机代码零改动。

## 目录结构

- **`archive_at32/`**：AT32 方案的全部历史资料（**冻结，不再更新**）。
  - `agent_at32.md`：AT32 下位机开发经验全集（方案更替原因、接口约定、INAV 配置、自研固件路线、硬件教训）。
  - `AT32F435CGU7_project/`：AT32 Workbench 生成的工程骨架（外设初始化 + 9 个空任务；**非可飞固件**）。
  - `下位机MCU手册.pdf` / `.docx`：AT32 板子使用手册。
  - `.idea/`：IDE 配置（忽略）。
- **`new_mcu/`**：**新主控（无刷 + 自带 ESC）**的内容（选型/固件/驱动/联调），待硬件到位后填充。见 `new_mcu/README.md`。

## 接口约定（跨主控不变）

| 接口 | 用途 | 参数 |
|---|---|---|
| UART1 | 上位机 MSP | **460800**（`SET_RAW_RC` 注入 + `RAW_IMU`/`ATTITUDE` 回读） |
| UART7 | CRSF 数字接收机（物理 RC，优先） | 4800 |
| UART5 | 光流计 MTF-02P | 115200 |
| SPI1 | IMU（LSM6DSOWTR） | — |
| I2C2 | 磁力计 QMC5883P + 气压计 SPL06-001 | — |

- **上位机统一接口**：`TargetInfo{ 偏移X, 偏移Y, 面积Area(∝1/距离²), 状态Status }`；
  链路层只换编码器（近期 MSP 摇杆值 / 远期自定义帧 `AA 55 | X | Y | Area | Status | CRC8`），**上层逻辑两阶段零改动，主控更替也不影响**。
- **RC 优先**：物理 CRSF 直连为准，拨杆切换自主/手动；上位机停发时回落物理 RC（fail-safe）。

## 参考

- 上位机：`../Host_Lichee_RV_Nano/`（总览、`AGENT_GUIDE.md`、操作手册 `diary.md`）
- 项目总览 / AI 上下文：`../README.md`、`../agent.md`
- AT32 历史与教训：`archive_at32/agent_at32.md`
