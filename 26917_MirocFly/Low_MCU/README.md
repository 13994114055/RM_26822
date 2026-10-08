# MirocFly 下位机（Low_MCU）

> 本目录是 MirocFly 的**下位机（飞行控制器，执行器）**开发区。
> 上位机（LicheeRV Nano，视觉/决策）在 `../Host_Lichee_RV_Nano/`。

## 当前状态（截至 2026-09-30）

- **原 AT32F435 方案已淘汰**：直驱空心杯实测升力约 59g < 整机 76g，**无法起飞**。
- **新主控已定型**：**BetaFPV G473**（STM32G474，无刷 + 四合一 DSHOT ESC）。与**此前烧毁的板子同款**，已**重新购入**。
- **固件：自研移植的 INAV 10.0.0，当前“能飞”**（官方 INAV 不支持 STM32G4，本固件为 G4 移植版）。工作区见 `new_mcu/drone/`，详情见 `new_mcu/README.md`。
- **尚未“本土化改造”**：未适配**上位机 MSP 对接**、未适配**光流**，**接口/UART 映射未细看、未定**。
  - ⚠️ 因此下表的历史接口约定**尚未在新板子上落实**，**不要默认仍然成立**。

## 目录结构

- **`archive_at32/`**：AT32 方案的全部历史资料（**冻结，不再更新**）。
  - `agent_at32.md`：AT32 下位机开发经验全集（方案更替原因、接口约定、INAV 配置、自研固件路线、硬件教训）。
  - `AT32F435CGU7_project/`：AT32 Workbench 生成的工程骨架（外设初始化 + 9 个空任务；**非可飞固件**）。
  - `下位机MCU手册.pdf` / `.docx`：AT32 板子使用手册。
  - `.idea/`：IDE 配置（忽略）。
- **`new_mcu/`**：**新主控 BetaFPV G473（STM32G474）**的工作区。含 `drone/`（INAV 10.0.0 G4 移植源码树 + 构建产物 + Betaflight 参考 + 工具）。见 `new_mcu/README.md`。
  - `drone/inav/`：自移植的 INAV 源码（含 STM32G4 支持与自制 `target/BETAFPVG473`）。
  - `drone/firmware/BETAFPVG473-motor-af-i2c-fix/`：可烧写固件（hex/bin/elf）。
  - `drone/betafpvg47/`：Betaflight 4.5.1 参考 + 机架模型。

## 接口约定（⚠️ 历史约定，新板尚未落实）

> 下表是 **AT32 阶段的历史约定**，也是上位机的**逻辑接口目标**；
> 但**新主控 BetaFPV G473 的 UART 资源不同**（只有 UART1~4 + VCP，无 UART5/7，默认 CRSF 在 UART3），
> **接口/UART 映射尚未细看、未重新定义**。**落地前以 `new_mcu/README.md` 的“已知信息/待办”为准。**

| 接口（历史） | 用途 | 参数 |
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
