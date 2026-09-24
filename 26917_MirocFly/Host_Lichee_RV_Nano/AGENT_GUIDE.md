# AGENT_GUIDE — 上位机（LicheeRV-Nano / SG2002）开发指导

> 本文件专供后续 AI Agent 在**上位机**文件夹内工作时使用。请先读完本文件，再动手改代码。

## 1. 项目总体方向（双重目标）

- **技术目标**：自主撞击不规则物体，30 秒内返航。
- **就业目标**：一次项目同时展示「控制 + Linux + 边缘AI部署」复合能力。
- **原则**：就业价值来自任务刚需本身，不是"加戏"。最终任务（识别不规则物体并决策飞行）天然要求边缘AI部署能力。

## 2. 上位机在系统中的职责（全部开发工作量所在）

```
LicheeRV Nano (SG2002) —— Linux视觉层: 采集→识别→目标选择→任务决策
        │  近期: MSP (SET_RAW_RC 注入 + RAW_IMU/ATTITUDE/STATUS 回读)
        │  远期: 0xAA 0x55 | X:int16 | Y:int16 | Area:uint16 | Status:uint8 | CRC8
        ▼
AT32F435 —— 近期: INAV(黑盒稳定器)  远期: 自写固件(姿态解算+PID+状态机)
```

上位机是**整个系统的大脑**：视觉感知、目标选择、任务状态机、撞击检测、返航决策全部在这里。下位机（近期）只是稳定的执行器。

**关键接口抽象**：上位机内部统一产出 `TargetInfo{像素偏移X, 像素偏移Y, 面积Area(∝1/距离²), 状态Status}`，链路层只换"编码器"——
- 近期编码为 **MSP 摇杆值**（`MSP_SET_RAW_RC`，10Hz）；
- 远期编码为 **自定义帧** `AA 55 | X | Y | Area | Status | CRC8`。
上层任务逻辑两阶段**零改动**。

## 3. 阶段路线图与当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| **0 工具链** | 交叉编译/板载编译 opencv-mobile + `test_camera.cpp` → 板上出帧存 JPEG；应用 `cvi_frames_read.patch` 获得 `VIDEO_FRAME_INFO_S` | 🔄 进行中（未验证出帧） |
| **1 绿色荧光（过渡）** | HSV 阈值检测荧光绿目标 → 多目标选择 → 像素偏移→摇杆修正（ANGLE模式）→ 经 MSP 注入 INAV 飞行验证 | ⏳ 未开始 |
| **2 不规则物体（最终）** | YOLOv8n 自定义训练 → INT8 cvimodel → TDL SDK 部署 NPU → 撞击检测 → 自稳 → 30s 内返航 | ⏳ 未开始 |
| **3 自写固件（远期）** | 上位机链路层切换到自定义帧编码器，上层逻辑零改动 | ⏳ 远期 |

**当前最重要的未完成项（阶段0）**：把 `test_camera.cpp` 在板子上编译跑通并拿到一帧图像。这是整个视觉链路的第一个可验证节点。

## 4. 已确认的技术底座（勿重复造轮子）

- **opencv-mobile 预编译静态库**：`opencv-mobile-5.0.0-licheerv-nano/`（riscv64-linux-musl，含 core/imgproc/video 等 .a），`bin/setup_vars_opencv5.sh` 设环境变量。
- **摄像头直出推理帧**：`CVI_HW_OpenCV/patches/cvi_frames_read.patch` 让 opencv-mobile 的 `VideoCapture` 额外暴露 `cap.image_ptr`（`VIDEO_FRAME_INFO_S*`），供 TDL SDK 推理复用。
- **边缘AI参考实现**：`ret7020/LicheeRVNano` 仓库的 `Projects/YoloCamera`（CSI + YOLOv8n + NPU，640×640 约 17–27 FPS）；官方 SDK 为 **sophgo tdl_sdk**（SG2002 NPU 代号 cv181x）。
- **模型量化**：SG2002 NPU 仅支持 **INT8 / BF16**（实测 BF16 YOLO 检测失败，用 INT8）。转换链 PyTorch→ONNX→MLIR→INT8 cvimodel，参考 ret7020 提供的 Colab notebook。
- **交叉编译链**：`LicheeRV-Nano-Build/host-tools/`、`CVI_HW_OpenCV/toolchains/riscv64-unknown-linux-musl.toolchain.cmake`。
- **板上访问**：USB RNDIS 网卡 → SSH `root@10.222.2.1`（详见 `diary.md`）。

## 5. 通信协议要点（近期 MSP）

- INAV 下位机串口**只支持 MSP**（已确认）。通道注入用 `MSP_SET_RAW_RC`（id 200），最低刷新 **5Hz**，推荐 **10Hz**。
- 回读：`MSP_RAW_IMU`(102) 用于撞击检测、`MSP_ATTITUDE`(108)、`MSP_RX_MAP`（通道映射）、`MSP2_INAV_STATUS`、`MSP2_INAV_ESTIMATED_POSITION`（光流位置，返航辅助）。
- **RC 优先机制**：依赖 INAV 的 `MSP RC Override` 飞行模式（需 INAV ≥8.1，`msp_override_channels` 掩码选 AETR）。飞行员在遥控器上拨杆切换自主/手动。上位机注入前应回读模式状态，确认 Override 生效再注入。
- **前置验证工具**：stronnag/msp_set_rx（golang，演示 `MSP_SET_RAW_RC` 用法）、INAV Configurator 观察通道"跳舞"。

## 6. 代码风格约定

- 优先使用现有库/参考实现，不重复造轮子。
- 上位机工程建议结构（待建 `app/`）：
  ```
  app/
  ├── link/     MSP客户端（串口+协议+回读）→ 远期换自定义帧编码器
  ├── vision/   capture(出帧) + detector(HSV→YOLO) + 目标选择
  ├── control/  视觉误差→摇杆修正量（ANGLE模式）
  ├── mission/  状态机(IDLE→ARM→TAKEOFF→SEEK→APPROACH→IMPACT→RECOVER→RTH→LAND) + 30s计时 + 撞击检测
  └── main.cpp  主循环
  ```
- 任务逻辑与协议编码解耦（为远期切换预留）。

## 7. 已知风险与待解决项

- 板上 INAV 版本是否 ≥8.1（决定 MSP Override 方案是否可用）——**需最先确认**。
- MTF-02P 光流与 INAV 协议兼容性未知 → 返航首选指令死推算，光流位置仅作辅助。
- NPU INT8 对自定义目标的精度 → 保留 HSV 作为调试/兜底。
- 撞击检测用 MSP_RAW_IMU 轮询（采样率受限），与视觉"bbox 达阈值"联合判定。