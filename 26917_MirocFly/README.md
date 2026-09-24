# MirocFly — 自主撞击无人机（LicheeRV-Nano + AT32F435）

## 项目概述

MirocFly 是一架具备**自主视觉撞击 + 快速返航**能力的室内无人机，基于双计算机架构：

- **上位机**：Sipeed LicheeRV-Nano（SG2002，RISC-V，1TOPS NPU）—— 系统"大脑"
- **下位机**：AT32F435（Cortex-M4，288MHz）—— 飞行执行器

**核心任务**：通过 CSI 摄像头捕捉画面，识别画面中的目标（不规则物体，可能多个，需分析选择），自主飞向目标并撞击，撞击后自稳，并在 **30 秒内**返回起飞点（"家"）。

## 项目双重目标

| 目标 | 内容 |
|---|---|
| 技术目标 | 自主撞击不规则物体，30 秒内返航 |
| 就业目标 | 一次项目同时展示「控制 + Linux + 边缘AI部署」复合能力 |

就业价值来自任务刚需本身：识别不规则物体必须部署边缘 AI（YOLO 到 NPU），飞控与闭环展示控制能力，整个感知-决策-飞行链路运行在嵌入式 Linux 上。

## 系统架构

```
LicheeRV Nano (SG2002)  ──  Linux视觉层: 采集→识别→目标选择→任务决策
        │  近期: MSP (SET_RAW_RC 注入 + RAW_IMU/ATTITUDE 回读)
        │  远期: 0xAA 0x55 | X | Y | Area | Status | CRC8
        ▼
AT32F435  ──  近期: INAV(黑盒稳定器)
        │      远期: 自写固件(姿态解算 + PID + 状态机 + 撞击检测 + 返航)
        │
        ├── UART7 CRSF 数字接收机（物理RC直连，RC优先接管）
        ├── UART5 光流计 MTF-02P
        ├── SPI1  IMU LSM6DSOWTR
        ├── I2C2  磁力计 QMC5883P + 气压计 SPL06-001
        └── TMR2  4通道PWM（空心杯电机）
```

**设计原则**：上位机 = 大脑（视觉/决策），下位机 = 稳定执行器；任务逻辑与通信协议解耦，两阶段演进时上层逻辑零改动。

## 任务流程（30 秒预算）

```
IDLE → ARM → TAKEOFF → SEEK(搜索目标) → APPROACH(接近撞击)
     → IMPACT(撞击检测) → RECOVER(自稳1s) → RTH(返航) → LAND
预算：接近 ~8s / 撞击自稳 ~5s / 返航 ~15s
```

## 开发阶段

| 阶段 | 内容 | 状态 |
|---|---|---|
| **0 工具链** | 交叉编译 + opencv-mobile + 摄像头出帧 | 🔄 进行中 |
| **1 绿色荧光（过渡）** | HSV 阈值检测 → 视觉→控制闭环（INAV 飞行验证） | ⏳ 未开始 |
| **2 不规则物体（最终）** | YOLOv8n 部署到 NPU → 撞击 → 自稳 → 30s 返航 | ⏳ 未开始 |
| **3 自写固件（远期）** | 自研飞控 + 自定义帧协议，全栈闭环 | ⏳ 远期 |

- **阶段 1 是过渡**：单色目标用 HSV 是为了快速验证感知→控制→飞行链路、降低风险；**阶段 2 才是最终形态**（不规则物体无法用阈值分割，必须学习型检测器）。
- **近期下位机用 INAV**（预编译，仅配置不改固件）；**远期自研飞控**，展示完整"控制"能力。

## 关键决策（已确认）

- **通信协议**：近期 = **MSP**（INAV 串口仅支持 MSP）；远期 = 自定义帧 `0xAA 0x55 | X | Y | Area | Status | CRC8`。
- **RC 优先接管**：INAV `MSP RC Override` 飞行模式，飞行员用遥控器拨杆切换自主/手动，硬件级兜底。
- **边缘 AI**：YOLOv8n → INT8 量化 → TDL SDK 部署 SG2002 NPU（官方示例 ~20FPS），HSV 仅作调试/兜底。
- **撞击检测**：近期上位机轮询 `MSP_RAW_IMU` 加速度突增；远期下位机 IMU 检测 + 状态机。
- **返航**：无 GPS，指令死推算为主 + 光流位置辅助。

## 硬件规格速览

- 上位机：LicheeRV-Nano（SG2002，256MB DDR3，1TOPS NPU，MIPI-CSI 摄像头，USB-RNDIS 网络）
- 下位机：AT32F435（288MHz 超频，1MB Flash / 128KB SRAM，FreeRTOS）
- 关键引脚：UART1(PA9/PA10)=上位机、UART7(PC7/PC8)=CRSF、UART5(PB8/PB9)=光流、SPI1(PA4-7)=IMU、I2C2(PB10/PB11)=磁力计/气压计、TMR2 4通道=电机
- 波特率：UART1=460800、UART5=115200、UART7=4800

## 当前状态与下一步

- **已完成**：Linux 镜像烧录并 SSH 可登录；opencv-mobile 预编译库就绪；AT32 Workbench 工程骨架（外设初始化 + FreeRTOS 任务骨架）；INAV 固件已刷入。
- **当前卡点**：摄像头出帧尚未板上验证。
- **下一步**：① 查板上 INAV 版本（确认 MSP Override 可用）；② 板上跑通 `test_camera.cpp` 拿到帧；③ 配置 INAV 并搭建上位机 MSP 客户端。

## 文档导航

- `Host_Lichee_RV_Nano/AGENT_GUIDE.md` — 上位机开发指导（供后续 agent）
- `Low_MCU/AGENT_GUIDE.md` — 下位机开发指导（供后续 agent）
- `agent.md` — AI Agent 工作空间上下文（决策/协议/进度）
- `Host_Lichee_RV_Nano/diary.md` — 板上操作日记（烧录/SSH）

---
*本项目遵循 "Keep It Simple" 原则：优先使用现有库与官方示例，测试永远渐进（拔桨→绑绳→短飞），飞行安全第一。*