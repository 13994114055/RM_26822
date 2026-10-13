# MirocFly — 自主撞击无人机（LicheeRV-Nano 上位机 + 无刷下位机）

## 项目概述

MirocFly 是一架具备**自主视觉撞击 + 快速返航**能力的室内无人机，基于**双计算机架构**：

- **上位机**：Sipeed LicheeRV-Nano（SG2002，RISC-V，1TOPS NPU）—— 系统"大脑"（视觉/决策）
- **下位机**：飞行控制器 —— 飞行执行器（近期 INAV，远期自研）

**核心任务**：通过 CSI 摄像头捕捉画面，识别目标（不规则物体，可能多个，需分析选择），自主飞向目标并撞击，撞击后自稳，并在 **30 秒内**返回起飞点（"家"）。

> ⚠️ **下位机方案更替（重要）**：最初选用 **AT32F435 直驱空心杯电机**，实测**升力仅约 59g** 而**整机约 76g** → **无法起飞**。现新主控定为 **BetaFPV G473（STM32G474，无刷 + 四合一 ESC）**，与**此前烧毁的板子同款、已重购**；固件为**自研移植的 INAV 10.0.0（STM32G4）且当前能飞**。**但尚未"本土化"**：未对接上位机 MSP、未适配光流，**接口/UART 映射未重定**（故旧接口约定**不再默认成立**）。AT32 阶段经验已存档于 `Low_MCU/archive_at32/agent_at32.md`（已冻结）；新主控详情见 `Low_MCU/new_mcu/README.md`。

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
下位机 BetaFPV G473 (STM32G474) —— 近期: INAV 10.0.0(自移植, 已能飞)
        │                                 远期: 自研飞控(姿态解算+PID+状态机+撞击检测+返航)
        ├── UART?  MSP（上位机）        ← 新板待重定
        ├── UART?  CRSF 数字接收机（物理RC，优先接管）← 新板待重定
        ├── UART?  光流计 MTF-02P
        ├── SPI1   IMU
        └── I2C?   磁力计 + 气压计
```

> ⚠️ 图中 `UART?` 表示**新主控 G473 的接口映射尚未重定**：该板只有 UART1~4 + VCP（无 UART5/7），默认 CRSF 在 UART3。详见 `Low_MCU/new_mcu/README.md`。

**设计原则**：上位机 = 大脑（视觉/决策），下位机 = 稳定执行器；任务逻辑与通信协议解耦——**两阶段演进、甚至下位机主控更替，上层逻辑都零改动**。

## 任务流程（30 秒预算）

```
IDLE → ARM → TAKEOFF → SEEK(搜索目标) → APPROACH(接近撞击)
     → IMPACT(撞击检测) → RECOVER(自稳1s) → RTH(返航) → LAND
预算：接近 ~8s / 撞击自稳 ~5s / 返航 ~15s
```

## 开发阶段与当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| **0 相机出帧** | 交叉编译 + CSI 抓帧（CVI MMF） | 🟢 **打通**：`experiments/…/04_vision_module`（Form A）出真图；`05_rtsp_stream` 已能 RTSP 实时预览 |
| **1 绿色荧光（过渡）** | HSV 检测 → 视觉→控制闭环（INAV 验证） | 🟢 **台架/板端全通**：检测(06)+链路(07)+打磨(14)+可视化(15)+撞击(16)+状态机干跑(17)+**操控下位机(18, MSP RC Override)**+**光流阻尼(19)**+**接管三轴/范式选择(20)**；台架真机标定 kp/kd 待做 |
| **2 不规则物体（最终）** | YOLOv8n → INT8 → NPU 部署 → 撞击 → 30s 返航 | 🟡 **部署(09/10)+量化(11)已通**；**自训装甲模型待 4060** |
| **3 自写固件（远期）** | 下位机自研飞控 + 自定义帧，全栈闭环 | ⏳ 远期（新主控 G473 已重购、INAV G4 能飞，待与上位机对接） |

- **阶段 1 是过渡**：单色目标用 HSV 快速验证"感知→控制→飞行"链路、降低风险；**阶段 2 才是最终形态**（不规则物体无法阈值分割，必须学习型检测器）。

## 关键决策（已确认）

- **通信协议**：近期 **MSP**（INAV 串口仅支持 MSP）；远期自定义帧 `0xAA 0x55 | X | Y | Area | Status | CRC8`。
- **RC 优先接管**：INAV 的 `MSP RC Override` 模式（固件自带 `BOX_MSP_RC_OVERRIDE` + `msp_override_channels`，**需在 CLI 配置、默认不覆盖**），遥控器拨杆切换，硬件级兜底。**开关映射（2026-10-12 定稿）**：`ch5`=解锁；**`ch6>1300 或 ch8>1300` = 上位机接管三轴**（同 modeId 挂两条 aux = **OR**）；`ch8>900`=ANGLE；`ch6`=追踪范式（当前全 **B**，`[1700,2100)` 预留 A）；`ch7 [1300,1700)`=**定点** / `[1700,2100)`=**定高**（定高暂留桩，需接管油门）。**⚠️ 掩码位序 = 内部通道号 AERT**（`ROLL=0 PITCH=1 YAW=2 THROTTLE=3`，非负载 AETR）：只接管三轴、油门留飞行员 = **`7`**（`11` 会误接管油门→切油）。
- **边缘 AI**：YOLOv8n → INT8 量化 → TDL SDK 部署 SG2002 NPU（同芯片实测 ~17–27 FPS），HSV 仅调试/兜底。
- **相机方案**：**用 scpcom 镜像 + scpcom 中间件**（官方镜像 GC4653 中间件坏；相机硬件本身正常）。自写程序必须**用 scpcom 的公有头编译、链接板上 scpcom 的库**，**不可混用 sipeed/官方中间件**（混用会导致取帧全黑）。
  - **相机光学（GC4653 官方镜头）**：焦距 **3.05mm** / F2.5 / **畸变 5%** / FOV **对角 90°、水平 81°、竖直 51°**（1/3"，2560×1440，2.0µm）。固定前视装法：**pitch 下倾约 10–15°**（竖直 FOV 宽、对角度不敏感），**倒装用 `MIROCFLY_CAM_ROT=180`**，俯仰基准用 `pitch_bias_px` 标定。
- **撞击检测**：近期上位机轮询 `MSP_RAW_IMU` 加速度突增；远期下位机 IMU 检测 + 状态机。
- **返航**：无 GPS，指令死推算为主 + 光流位置辅助。

## 硬件规格速览

- **上位机**：LicheeRV-Nano（SG2002，256MB DDR3，1TOPS NPU，MIPI-CSI 摄像头，USB-RNDIS 网络）
- **下位机（新）**：**BetaFPV G473（STM32G474，BMI270 IMU，四合一 DSHOT ESC）**，与烧毁的同款**已重购**；**自研移植 INAV 10.0.0（STM32G4）已能飞**；**本土化已落地**：上位机 MSP（UART2 @230400）链路已打通；**2026-10-10 解决"离地即翻/自旋"**（根因＝**电机输出编号与物理接线错位**；mixer 轮换版 + `motor_direction_inverted=OFF` + `align_board_roll=1800`），现**能起飞、不再自旋**。详见 `Low_MCU/new_mcu/AGENT_GUIDE.md`
- **下位机（原，已淘汰）**：AT32F435（288MHz 超频，1MB Flash/128KB SRAM，直驱空心杯——升力不足）
- **接口约定（⚠️ AT32 阶段历史约定，新板未落实）**：UART1=MSP(上位机)、UART7=CRSF(RC)、UART5=光流、SPI1=IMU、I2C2=磁力计/气压计；波特率 UART1=460800、UART5=115200、UART7=4800。**新板 G473 资源不同（仅 UART1~4+VCP），需重映射**

## 当前状态与下一步

- **已打通**：上位机镜像（scpcom）SSH 可登录；**相机链路全通**——板载 `test_mmf` 出真图，且**自写相机层 `experiments/2026-09-26_04_vision_module/`（Form A）出真图**、**`experiments/2026-09-26_05_rtsp_stream/` 打通 RTSP 实时预览**、**`experiments/2026-09-26_06_hsv_green/` 打通 HSV 绿色检测（输出 offset/area）**、**`experiments/2026-09-26_07_msp_link/` 打通 MSP 链路（PTY 模拟 FC 自测）**、**`experiments/2026-09-26_08_vision_control/` 写出"识别绿色并飞向"闭环程序**；**阶段 2 的 NPU 部署与量化通路已通**（`experiments/2026-09-26_09_npu_demo/`：底层 cviruntime + 高层 TDL，官方 YOLOv8n INT8 检测 17.6 FPS；`experiments/2026-09-27_10_tdl_infer/`：**可复用推理层 + 相机检测 + RTSP 叠加**；`experiments/2026-09-28_11_quantize/`：**TPU-MLIR 把 ONNX 量化成 cv181x INT8 cvimodel，板上 NPU 43.9 FPS**）；`experiments/2026-09-29_12_armor_led/`：**传统 CV 装甲板（红/蓝 LED 灯条）PC 原型**，合成集召回/精确 100%、中心误差 0.34px，不依赖训练）。已克隆 scpcom 源码树 `LicheeSG-Nano-Build_scpcom`；opencv-mobile/TDL 参考就绪。**闭环系列 14–20 已台架/板端打通**：14 EMA+丢检滞回+FC遥测、15 RTSP 叠加、16 IMU 撞击、17 状态机干跑、**18 操控下位机（MSP RC Override，台架+静态验证）**、**19 光流水平阻尼（数据通路已验）**、**20 接管三轴+范式/定点/定高选择（`mon`/`dry` 已验证）**；索引见 `experiments/README.md`。
- **黑帧根因（已解）**：**两问题叠加**——① 中间件版本混用（链了 sipeed，须用 **scpcom 公有头 + 板上 scpcom 库**）；② 抓到**流水线启动首帧**（须先丢前导帧）。次要：① 输出文件自动清理（限制 SD 占用）；② 下位机主控更替已落地（G473 能飞、MSP 链路已通、**方向类问题已解决**）。进度见 `Low_MCU/README.md`。
- **下一步**：① **上位机接管三轴飞行**（`experiments/2026-10-12_20_paradigm_select/`）——`mon`/`dry` 已验开关与模式，**待台架（压重物/绑绳）验 B 追踪/定点/基础阻尼 → 真机短飞**；② 光流质量一般（`q≈40–60`）→ 换纹理地面/亮光/标定 `opflow_scale`；③ 上位机测距**定高**（接管油门 `mask=15`）以支撑"巡航保持 1–1.5m、接近再降"的飞行剖面；④ NPU 检测接相机实时帧（`09` 步骤3）；⑤ 自训/量化（4060）；⑥ 调 PID 压 ~15.6Hz 姿态振荡。

## 文档导航

- **`commands.md`** — **常用命令速查**（板子在不在线 / 跑某功能 / FC 配置 / 黑匣子解码等，基础命令自己查）
- `Host_Lichee_RV_Nano/README.md` — **上位机目录总览**（项目角色 + 文件夹说明）
- `Host_Lichee_RV_Nano/experiments/README.md` — **上位机实验档案索引**（01~20，每次尝试一个目录）
- `Host_Lichee_RV_Nano/AGENT_GUIDE.md` — 上位机开发指导（持续更新）
- `Host_Lichee_RV_Nano/LicheeSG-Nano-Build_scpcom/` — **scpcom 源码树**（与板上镜像同源；相机样例在 `middleware/sample/test_mmf/`）
- `Low_MCU/README.md` — **下位机总览**（接口约定 + 新主控 `new_mcu/`）
- `Low_MCU/archive_at32/agent_at32.md` — 下位机 AT32 经验存档（**已冻结**）
- **`tools/README.md`** — **调试/分析工具集**（`msp_probe` 链路验证、`msp_live.py` 实时监控、`telem_mon` 板上只读遥测、`blackbox_decode.py` 黑匣子解码）
- `agent.md` — AI Agent 工作空间上下文（决策/协议/进度）
- `Host_Lichee_RV_Nano/difficulty_and_method.md` — 困难与解决办法（复盘/面试用）
- `Host_Lichee_RV_Nano/diary.md` — **上位机操作手册**（如何编译/部署/运行各功能与排错）

## 第三方依赖（不入库，需自行获取）

以下目录**体积庞大或本身是独立 git 仓库**，已加入根 `.gitignore`，**不随本仓库提交**。克隆本仓库后，请按下表获取并放回**同名路径**，否则相关实验无法编译：

| 目录 | 说明 | 获取方式 |
|---|---|---|
| `Host_Lichee_RV_Nano/LicheeRV-Nano-Build_official/` | Sipeed 官方 SDK（**交叉编译工具链** `host-tools/gcc/riscv64-linux-musl-x86_64/` + 内核头 `linux_5.10/.../usr/include`） | 从 Sipeed 官方 SDK 获取（约 36G；请补充你实际使用的仓库地址） |
| `Host_Lichee_RV_Nano/LicheeSG-Nano-Build_scpcom/` | **scpcom 版 SDK**（与板上镜像同源；相机/RTSP 样例、`tdl_sdk`） | `git clone https://github.com/scpcom/LicheeSG-Nano-Build` |
| `Host_Lichee_RV_Nano/opencv-mobile-5.0.0-licheerv-nano/` | opencv-mobile 预编译库（检测程序链接用） | opencv-mobile 的 **v5.0.0 licheerv-nano** 预编译包（请补充来源地址） |
| `Host_Lichee_RV_Nano/CVI_HW_OpenCV/` | 硬件加速 OpenCV 参考仓库（参考资料） | `git clone https://github.com/ret7020/CVI_HW_OpenCV.git` |

> 板上库链接副本 `Host_Lichee_RV_Nano/board_libs/` 同样不入库，用 `Host_Lichee_RV_Nano/fetch_board_libs.sh` 从板子拉取。
> 详见 `Host_Lichee_RV_Nano/README.md` 与操作手册 `Host_Lichee_RV_Nano/diary.md`。

---
*本项目遵循 "Keep It Simple" 原则：优先使用现有库与官方示例，测试永远渐进（拔桨→绑绳→短飞），飞行安全第一。*
