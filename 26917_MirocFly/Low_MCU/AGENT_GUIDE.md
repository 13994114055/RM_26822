# AGENT_GUIDE — 下位机（AT32F435）开发指导

> 本文件专供后续 AI Agent 在**下位机**文件夹内工作时使用。请先读完本文件，再动手改代码。

## 1. 项目总体方向（双重目标）

- **技术目标**：自主撞击不规则物体，30 秒内返航。
- **就业目标**：展示「控制 + Linux + 边缘AI部署」复合能力。
- **下位机角色**：近期作为 INAV 黑盒稳定器（只配置，不改固件）；远期实现自研飞控（姿态解算 + PID + 状态机 + 撞击检测 + 返航），这是本项目"控制"能力的最终体现。

## 2. 两阶段架构（下位机侧）

```
近期（学长路线）:  AT32F435 刷 INAV（预编译，不可重编）
                    ├── UART1 = MSP（上位机）       ← 只支持 MSP，不支持 MAVLink
                    ├── UART7 = CRSF 数字接收机      ← 物理RC直连，RC优先
                    ├── UART5 = 光流计 MTF-02P (115200)
                    ├── SPI1  = IMU LSM6DSOWTR
                    └── I2C2  = 磁力计 QMC5883P + 气压计 SPL06-001

远期（文档架构）:  自写固件，上位机发自定义帧
                    AA 55 | X:int16 | Y:int16 | Area:uint16 | Status:uint8 | CRC8
                    MCU负责：姿态解算 + PID + 混控 + 状态机 + 撞击检测 + 30s返航
```

**关键约束**：
- 近期 INAV 是**预编译固件，不能重新编译**（无法加 `USE_MSP_RC_OVERRIDE` 编译开关）。
- 依赖 **INAV ≥8.1** 时 `MSP RC Override` 飞行模式默认启用（8.0.1 及更早需手动编译定义）。若板上版本过老，INAV 阶段只能做手动飞行/气动验证，自主视觉验证推迟到远期自写固件。
- INAV 阶段**禁止改动 INAV 固件**，所有能力均通过 CLI/Configurator 配置实现。

## 3. 当前工程状态（Workbench 骨架，非可飞固件）

工程位置：`Low_MCU/AT32F435CGU7_project/`

**已初始化**（`wk_*.c`）：
- `wk_usart.c`：UART1(460800, 上位机)、UART5(115200, 光流)、UART7(CRSF)
- `wk_spi.c`：SPI1（IMU）；`wk_i2c.c`：I2C2（磁力计/气压计）
- `wk_tmr.c`：TMR1（时间基准）、**TMR2 4通道PWM**（电机输出，ARR=14399，引脚：CH1/PA15、CH2/PB9、CH3/PA2、CH4/PA3）
- `wk_dma.c`：DMA1 多通道（UART1/5/7 收发）
- FreeRTOS：`freertos_app.c` 内 9 个**空任务**骨架：`pid/imu/i2c/hitdetect/vision/flow/rc/count30s/sm`

**当前实际状态**：仅 `vision_task` 内有一段 UART1 回环测试 + 单通道电机 PWM 示例。**无 IMU 读取、无姿态解算、无 PID、无 MSP 解析、无 CRSF 解析、无光流解析、无撞击检测、无状态机**——不能直接用于飞行。

## 4. 近期工作（INAV 配置，只改配置不改固件）

1. **确认板上 INAV 版本**（Configurator / CLI `version`）。≥8.1 → 走 `MSP RC Override` 方案；否则降级为"INAV 仅手动飞行验证"。
2. CLI/Configurator 配置：
   - UART1 = MSP（与上位机通信，460800）
   - UART7 = CRSF 接收机（RC 源）
   - `msp_override_channels` = AETR 掩码（仅遥控器拨杆在 Override 模式下由 MSP 覆盖摇杆通道）
   - 遥控器上把 `MSP RC Override` 模式映射到一个拨杆（自主/手动切换）
   - **关闭原生 `crash_detection_*`**（避免与任务撞击逻辑冲突）
   - 配置 fail-safe（上位机停发→回落物理 RC→安全动作）
3. 拔桨→绑绳测试：手动/自主拨杆切换、fail-safe 生效。

## 5. 远期工作（自写固件，最终架构）

在现有 Workbench 骨架上按依赖顺序实现，**每步拔桨→绑绳→短飞验证后再进下一步**：

| 步骤 | 任务 | 说明 |
|---|---|---|
| 3.1 | IMU + 姿态 | SPI1 读 LSM6DSOWTR @1kHz + 零偏校准 + Mahony 姿态解算 |
| 3.2 | PID + 混控 | 1kHz 角速率/角度 PID + X 混控 → TMR2 四通道输出 |
| 3.3 | CRSF + Override | UART7 CRSF 解析 + 解锁/解锁 + 复刻 INAV Override 语义（物理RC为准，自主拨杆才注入MSP通道） |
| 3.4 | 光流 + 位置 | UART5 MTF-02P 解析 + 位置积分（返航用） |
| 3.5 | 自定义帧 | UART1 解析 `AA 55 | X | Y | Area | Status | CRC8` + 回读遥测 |
| 3.6 | 撞击 + 状态机 | IMU 突增(>4–6g)或电机掉速判撞击 → 撞击后水平自稳 1s → 状态机 IDLE→ARM→TAKEOFF→SEEK→APPROACH→IMPACT→RECOVER→RTH→LAND + 30s 计时 |
| 3.7 | 集成 | 与上位机自定义帧编码器联调全任务 |

## 6. 代码风格约定

- 沿用 Workbench 的 `wk_*.c/h` 分层 + FreeRTOS 任务划分，任务函数已预建好骨架。
- 时钟：**288MHz 超频需验证稳定性，建议先降到 216MHz 起步**。
- 电机安全：解锁前所有通道占空比=0；任何错误状态必须禁能电机输出。
- RC Override 语义必须与 INAV 阶段一致（保证上位机逻辑两阶段零改动）。

## 7. 已知风险与待解决项

- 板上 INAV 版本未知 → **最先确认**。
- 288MHz 超频稳定性 → 216MHz 起步验证。
- MTF-02P 光流协议需自行解析（INAV 阶段不依赖它，远期固件自研）。
- 撞击后加速度计可能饱和，检测窗口要短且只触发一次。