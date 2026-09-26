# 新主控（new_mcu）

> 新下位机主控：**无刷电机 + 自带无刷驱动（ESC）**。硬件**尚未到手**，此处为占位，待选型后填充。

## 待办（硬件到位后）

- **选型确认**：主控型号、电机/ESC、供电、重量与**升力核算**（总升力 ≥ 1.5~2× 整机重量）。
- **接口复刻**（与 AT32 阶段一致，上位机零改动）：
  - UART1 = MSP @460800（上位机）；UART7 = CRSF（物理 RC）；UART5 = 光流；SPI1 = IMU；I2C2 = 磁力计/气压计。
- **近期策略**：INAV（只配置不改固件），用 `MSP RC Override` 接收上位机摇杆；物理 RC 拨杆优先、上位机停发回落。
- **远期**：自研飞控（姿态解算 + PID + 混控 + 状态机 + 撞击检测 + 30s 返航）。
- **联调顺序**：拔桨 → 绑绳 → 短飞；验证 rock/pitch 符号、摇杆量程、fail-safe。

## 参考

- 接口约定与经验：`../archive_at32/agent_at32.md`
- 上位机 MSP 链路实现：`../../Host_Lichee_RV_Nano/experiments/2026-09-26_07_msp_link/`
- 上位机闭环程序（识别绿色并飞向）：`../../Host_Lichee_RV_Nano/experiments/2026-09-26_08_vision_control/`
