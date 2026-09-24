# Agent Workspace: MirocFly — 自主撞击无人机 (LicheeRV-Nano + AT32F435)

> 本文件是后续 AI Agent 的**全局工作空间上下文**。改动任何模块前，请同时查阅
> `Host_Lichee_RV_Nano/AGENT_GUIDE.md` 与 `Low_MCU/AGENT_GUIDE.md`。

## 1. 项目目标（双重目标）

- **技术目标**：自主撞击不规则物体，30 秒内返航。
- **就业目标**：一次项目同时展示「控制 + Linux + 边缘AI部署」复合能力（薪资溢价载体）。
- **原则**：就业价值来自任务刚需，不是加戏——识别不规则物体天然要求边缘 AI 部署。

## 2. 系统架构（两阶段演进）

```
LicheeRV Nano (SG2002) —— Linux视觉层: 采集→识别→目标选择→任务决策
        │  近期: MSP (SET_RAW_RC 注入 + RAW_IMU/ATTITUDE 回读)
        │  远期: 0xAA 0x55 | X:int16 | Y:int16 | Area:uint16 | Status:uint8 | CRC8
        ▼
AT32F435 —— 近期: INAV(黑盒稳定器, 预编译不可重编)
        │  远期: 自写固件(姿态解算+PID+混控+状态机+撞击检测+30s返航)
        │
        ├── UART7 CRSF 数字接收机（物理RC直连，RC优先）
        ├── UART5 光流计 MTF-02P (115200)
        ├── SPI1  IMU LSM6DSOWTR
        └── I2C2  磁力计 QMC5883P + 气压计 SPL06-001
```

**接口抽象**：上位机统一产出 `TargetInfo{偏移X, 偏移Y, Area(∝1/距离²), Status}`，链路层仅换编码器
（近期 MSP 摇杆值 / 远期自定义帧），上层任务逻辑两阶段零改动。

## 3. 关键决策记录（已敲定，勿推翻）

| 决策 | 内容 |
|---|---|
| 下位机策略 | 近期 INAV（学长方向，只配置不改固件）；远期自研飞控（最终架构，两阶段都要） |
| 通信协议 | 近期 **MSP**（INAV 串口仅支持 MSP，不支持 MAVLink）；远期自定义帧 `0xAA 0x55` |
| INAV 固件 | 预编译，**不能重新编译**（无法加 `USE_MSP_RC_OVERRIDE` 开关） |
| RC 优先 | INAV `MSP RC Override` 飞行模式（需 INAV ≥8.1）+ `msp_override_channels`=AETR；飞行员拨杆切换 |
| 视觉方案 | 阶段1 绿色荧光用 HSV（过渡，快速验证闭环）；阶段2 不规则物体用 **YOLOv8n→INT8→TDL SDK 部署 NPU**；HSV 仅调试/兜底 |
| 撞击检测 | 近期上位机轮询 `MSP_RAW_IMU`(102) 加速度突增(>4–6g) + 视觉 bbox 达阈值；远期下位机 IMU 检测 |
| 返航 | 无 GPS：指令死推算为主 + `MSP2_INAV_ESTIMATED_POSITION` 光流位置辅助 |
| 任务预算 | 30s：接近~8s / 撞击自稳~5s / 返航~15s |
| 超频 | 288MHz 需验证稳定性，自写固件阶段建议 216MHz 起步 |

## 4. 阶段状态

| 阶段 | 内容 | 状态 | 下一步 |
|---|---|---|---|
| 0 工具链 | 编译链 + opencv-mobile + 摄像头出帧 | 🔄 进行中 | 板上跑通 `test_camera.cpp` 拿到帧 |
| 1 绿色荧光 | HSV → 视觉→控制闭环（INAV） | ⏳ | 确认 INAV 版本→配置 MSP Override→搭建 MSP 客户端 |
| 2 不规则物体 | YOLOv8n NPU 部署 → 撞击 → 30s 返航 | ⏳ | 采集/标注/训练/量化 |
| 3 自写固件 | 自研飞控 + 自定义帧 | ⏳ 远期 | 见 Low_MCU/AGENT_GUIDE.md 第5节 |

## 5. 当前工程实况

**上位机** `Host_Lichee_RV_Nano/`
- Linux 镜像已烧录，USB-RNDIS SSH 可登录（`diary.md` 有操作记录）
- `opencv-mobile-5.0.0-licheerv-nano/` 预编译静态库（core/imgproc/video 等）
- `test_camera.cpp` 已写未验证；`CVI_HW_OpenCV/`（ret7020，含 `cvi_frames_read.patch` 出 `VIDEO_FRAME_INFO_S`）
- 参考实现：ret7020/LicheeRVNano `Projects/YoloCamera`（COCO YOLOv8n，NPU ~20FPS）

**下位机** `Low_MCU/AT32F435CGU7_project/`
- Workbench 骨架：外设已初始化（UART1/5/7、SPI1、I2C2、TMR1/2），FreeRTOS 9 个空任务骨架
- `vision_task` 内有 UART1 回环测试 + 单通道 PWM 示例（非飞行代码）
- 无 IMU/姿态/PID/MSP/CRSF/光流/撞击/状态机实现——**不可直接飞行**

## 6. 技术要点速查

- MSP 消息：`MSP_SET_RAW_RC`(200, ≥5Hz 推荐10Hz)、`MSP_RAW_IMU`(102)、`MSP_ATTITUDE`(108)、`MSP_RX_MAP`、`MSP2_INAV_STATUS`、`MSP2_INAV_ESTIMATED_POSITION`
- MSP Override：飞行模式 `MSP RC Override`，`msp_override_channels` 位掩码选通道，停发自动回落物理 RC
- NPU：SG2002 代号 cv181x，仅支持 INT8/BF16（BF16 YOLO 实测失败）；转换链 PyTorch→ONNX→MLIR→INT8 cvimodel（ret7020 Colab notebook）
- 光流 MTF-02P 协议与 INAV 兼容性未知 → INAV 阶段不依赖，远期固件自研解析

## 7. 给后续 Agent 的要求

- 改动任何模块前：先读对应 `AGENT_GUIDE.md`，保持与本文档决策一致。
- 不重复造轮子：优先官方 SDK 示例（TDL/YoloCamera）、已有库（opencv-mobile）。
- 上位机代码结构约定：`app/{link,vision,control,mission,main.cpp}`，任务逻辑与协议编码解耦。
- 下位机沿用 `wk_*.c/h` 分层 + FreeRTOS 任务划分，任务函数已预建骨架。
- 测试永远渐进：拔桨 → 绑绳 → 短飞；电机解锁前占空比=0；错误状态必须禁能电机。
- 只做被明确要求的改动，不擅自扩大范围。

## 8. 风险与待确认项（按优先级）

1. **板上 INAV 版本是否 ≥8.1**——决定 MSP Override 方案是否可行（最先确认）
2. 288MHz 超频稳定性
3. NPU INT8 对自定义目标精度（HSV 兜底）
4. 撞击检测采样率受限（MSP 轮询）→ 与视觉联合判定
5. INAV 原生 `crash_detection_*` 需关闭（避免与任务撞击逻辑冲突）