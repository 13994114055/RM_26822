# Agent Workspace: MirocFly — 自主撞击无人机 (LicheeRV-Nano + 下位机)

> 本文件是后续 AI Agent 的**全局工作空间上下文**。改动任何模块前，请同时查阅：
> - `Host_Lichee_RV_Nano/AGENT_GUIDE.md`（上位机，持续更新）
> - `Low_MCU/archive_at32/agent_at32.md`（下位机 AT32 经验存档，**已冻结**）
> - `Host_Lichee_RV_Nano/difficulty_and_method.md`（困难与解决办法）

## 1. 项目目标（双重目标）

- **技术目标**：自主撞击不规则物体，30 秒内返航。
- **就业目标**：一次项目同时展示「控制 + Linux + 边缘AI部署」复合能力（薪资溢价载体）。
- **原则**：就业价值来自任务刚需，不是加戏——识别不规则物体天然要求边缘 AI 部署。

## 2. 系统架构

```
LicheeRV Nano (SG2002) 上位机 —— Linux视觉层: 采集→识别→目标选择→任务决策
        │  近期: MSP (SET_RAW_RC 注入 + RAW_IMU/ATTITUDE 回读)
        │  远期: 0xAA 0x55 | X:int16 | Y:int16 | Area:uint16 | Status:uint8 | CRC8
        ▼
下位机 BetaFPV G473 (STM32G474) —— 近期: INAV 10.0.0(自移植, 已能飞)
        │  远期: 自研飞控(姿态解算+PID+混控+状态机+撞击检测+30s返航)
        │
        ├── UART? CRSF 数字接收机（物理RC，RC优先）   ← 新板映射未定
        ├── UART? 光流计 MTF-02P                     ← 新板映射未定
        ├── SPI1  IMU
        └── I2C?  磁力计 + 气压计                    ← 新板映射未定
```

**⚠️ 下位机硬件已更替**：原 **AT32F435 直驱空心杯**方案动力不足（**升力约 59g < 整机 76g，无法起飞**）→ 新主控 **BetaFPV G473（STM32G474，无刷 + 四合一 ESC）**，与**烧毁的板子同款、已重购**；**自研移植 INAV 10.0.0（STM32G4）已能飞**。**但尚未本土化**（上位机 MSP / 光流 / 接口映射未定），**旧接口约定不再默认成立**。AT32 经验存档见 `Low_MCU/archive_at32/agent_at32.md`（已冻结）；新主控见 `Low_MCU/new_mcu/README.md`。

**接口抽象**：上位机统一产出 `TargetInfo{偏移X, 偏移Y, Area(∝1/距离²), Status}`，链路层仅换编码器（近期 MSP 摇杆值 / 远期自定义帧），上层任务逻辑两阶段零改动，**下位机主控更替也不影响上位机**。

## 3. 关键决策记录（已敲定，勿推翻）

| 决策 | 内容 |
|---|---|
| 下位机策略 | 近期 INAV（只配置不改固件）；远期自研飞控 |
| **下位机硬件** | **BetaFPV G473（STM32G474，无刷 + 四合一 ESC）**，与烧毁的同款已重购；自移植 INAV 10.0.0(G4) 能飞（原 AT32 直驱空心杯升力不足被淘汰） |
| 通信协议 | 近期 **MSP**（INAV 串口仅支持 MSP）；远期自定义帧 `0xAA 0x55` |
| INAV 固件 | 新板 **自移植 INAV 10.0.0（STM32G4，可重编）**；旧 AT32 板的 INAV 9.0.0 为不可重编的预编译版（随 AT32 淘汰） |
| RC 优先 | INAV `MSP RC Override` 模式（`BOX_MSP_RC_OVERRIDE` + `msp_override_channels`，**需 CLI 配置、默认不覆盖**）；飞行员拨杆切换 |
| 视觉方案 | 阶段1 绿色荧光 HSV（过渡）；阶段2 不规则物体 **YOLOv8n→INT8→TDL SDK→NPU**；HSV 仅兜底 |
| **相机方案** | **用 scpcom 镜像 + scpcom 中间件**（官方镜像 GC4653 中间件坏；相机硬件正常）。自写程序须用 scpcom 公有头编译 + 链接板上 scpcom 库，**禁止混用 sipeed/官方中间件**（混用→取帧全黑） |
| 撞击检测 | 近期上位机轮询 `MSP_RAW_IMU`(102) + 视觉 bbox 阈值；远期下位机 IMU 检测 |
| 返航 | 无 GPS：指令死推算为主 + `MSP2_INAV_ESTIMATED_POSITION` 光流辅助 |
| 任务预算 | 30s：接近~8s / 撞击自稳~5s / 返航~15s |

## 4. 阶段状态

| 阶段 | 内容 | 状态 | 下一步 |
|---|---|---|---|
| 0 相机出帧 | 交叉编译 + CSI 抓帧 | 🟢 **`experiments/…/04_vision_module` Form A 出真图**；`05_rtsp_stream` RTSP 预览打通（B7/B9/B11 已解） | 阶段 1（HSV）或 MSP 链路 |
| 1 绿色荧光 | HSV → 视觉→控制闭环（INAV） | 🟢 **台架闭环全通**：检测(06)+链路(07)+打磨(14 EMA/丢检滞回/FC遥测)+可视化(15 RTSP叠加)+撞击(16 IMU)+状态机干跑(17) | 上电池→短悬停校准 kp/符号/相机pitch偏置 |
| 2 不规则物体 | YOLOv8n NPU 部署 → 撞击 → 30s 返航 | 🟡 **部署(09/10)+量化(11)已通**；自训装甲模型待 4060 | 训练+导出 → 量化 → 部署 |
| 3 自写固件 | 下位机自研飞控 | ⏳ 远期 | 新主控 G473 已重购、INAV G4 能飞；待上位机对接后推进 |

## 5. 当前工程实况

**上位机** `Host_Lichee_RV_Nano/`
- **镜像 = scpcom 构建**（`/boot/ver`≈`6f9596`，内核 `2026-07-14`，中间件 `libsys 87be70b`）；源码树已克隆至 `LicheeSG-Nano-Build_scpcom/`（`scpcom/LicheeSG-Nano-Build`，sophgo weekly 2024.10.14 中间件）。
- 板上 `test_mmf`（scpcom 编）取**相机**帧 → 真图；**自写程序 `experiments/…/03_scpcom_formA`（Form A）也已出真图**（黑帧根因 = 中间件版本混用 + 抓启动首帧，均已在 `04_vision_module` 解决）。注：scpcom 版 `test_mmf 4` 走**相机**（`_test_venc_jpg` 用 `mmf_vi_frame_pop`），sipeed 版的才是合成彩条图。
- 默认 sensor 配置 = **beta**（`lane_id=4,3,2 / mclk=1 / bus4 / i2c 0x29`），由镜像 `/etc/init.d/S02config` 决定：无 `/boot/alpha|epsilon` → beta + pinmux（`devmem 0x0300116C=0x3, 0x0300118C=0x5`）。**我们的 `sensor_cfg.ini` 内容 = beta，并未改坏配置**。
- **实验档案制**（每次尝试一个目录，索引 `Host_Lichee_RV_Nano/experiments/README.md`）：01 低层MMF / 02 sipeed链 / 03 FormA首验 / 04 相机层 / 05 RTSP / 06 绿色检测 / 07 MSP 链路 / 08 识别绿色并飞向(闭环) / 09 NPU 部署(底层 cviruntime + 高层 TDL 检测) / 10 TDL 推理层(相机+检测+画框+RTSP) / 11 TPU-MLIR 量化(ONNX→cv181x INT8 cvimodel)。
- `experiments/2026-09-26_04_vision_module/`（✅相机层）：Form A——`camera.h` API + `frame_grab` CLI；编译 scpcom `maix_mmf/sophgo_middleware.c`，链接 `Host_Lichee_RV_Nano/board_libs/`（`fetch_board_libs.sh` 拉取，**不进板**；`-lsample` 等，**必须 `-lgdc`**）；`mf_camera_warmup(10)` 丢前导帧。
- `experiments/2026-09-26_05_rtsp_stream/`（✅RTSP）：VI→VENC(H265)→RTSP；复用 scpcom `rtsp_server/`+`media_server` 静态库；**必须显式绑 IP**（`RTSP_IP=10.222.2.1`，否则绑到 wlan0，见 B11）；仅 H265；退出用 SIGTERM 勿 `kill -9`。
- `experiments/2026-09-26_06_hsv_green/`（✅绿色检测）：opencv-mobile HSV；阈值 PIL H85-130/S40-170/V55-170 = OpenCV H60-91/S40-170/V55-170；轮廓函数在 **geometry** 模块(`-lopencv_geometry`)；无 imgcodecs 故**直接写 BMP**；小目标 `min_area=20`。
- `experiments/2026-09-26_07_msp_link/`（✅MSP 链路）：纯 C `msp.c`（MSP v1，csum=异或；`SET_RAW_RC=200`/`RAW_IMU=102`/`ATTITUDE=108`；`offset→RC` AETR）。**无硬件用 PTY 模拟 FC 自测**；真串口 UART1@460800 待下位机（`inject`/`monitor`）。
- `experiments/2026-09-26_08_vision_control/`（✅识别绿色并飞向，闭环雏形）：`green_fly` = 06 检测 + 07 链路；默认 PTY 假 FC(无硬件可跑)，给串口则注入真 FC；无目标回中、油门默认 1000。
- `experiments/2026-09-26_09_npu_demo/`（✅NPU 部署入门）：步骤1 `npu_hello`(底层 cviruntime，mobilenet 226 FPS)；步骤2 `tdl_detect`(高层 TDL，官方 yolov8n_det_coco80@640 INT8，**17.6 FPS**，检出 person 0.90)；步骤3 `vi_detect`(**相机实时检测 16.4 FPS**)。链接坑：TDL 需板上中间件符号→链 `board_libs`；`-lgcc_s` 解隐藏原子符号；**不链 `-ltdl_ex`**（它拉 libcurl，板上 libcurl/libssl 不匹配，见 B13）。
- 板上 NPU 运行时齐（`libcviruntime/cvikernel/cvimath` + `/dev/cvi-tpu0`），TDL 运行时在 `/mnt/system/lib/libtdl_{core,ex,utils}.so`；模型 `/usr/bin/{mobilenet_v2_rgb_224_int8,resize_net_3_int8,yolov5s_224_int8}.cvimodel`；官方 cv181x 模型仓库 `sophgo/tdl_models`。
- `experiments/2026-09-27_10_tdl_infer/`（✅可复用推理层）：`mf/tdl.h`(C 接口) + `mf_tdl.cpp`(相机/TDL检测/自绘框/RTSP)。RTSP `rtsp://10.222.2.1:554/h265`（H265），~16FPS。坑见 B14（`TDL_WrapImage` 传指针不是传结构体，否则 RTSP 花屏）。
- `experiments/2026-09-28_11_quantize/`（✅量化打通）：TPU-MLIR（docker 镜像走算能 CDN + `docker load`；容器内 `pip install tpu_mlir` 后 commit）。`resnet18.onnx` → `resnet18_cv181x_int8_sym.cvimodel`，板上 NPU 43.9FPS。三段式：`model_transform→run_calibration→model_deploy --processor cv181x --quantize INT8`。坑见 B15。量化在 Linux i5；训练在 Win11(4060)。
- `experiments/2026-09-29_12_armor_led/`（🟡传统CV装甲板 S0+S1）：**并行兜底/预研**，不依赖训练。S0 纯 PIL + 自解单应生成**透视梯形**合成图 21 张（含真值 `labels.csv`）；S1 `pc_tune/detect_armor.py`（**cv2**，PC venv 装 `opencv-python 5.0.0` 与板端 opencv-mobile 5.0.0 对齐）：通道差→形态学→minAreaRect→同色**松弛配对**→**四点四边形中心**，合成集**召回/精确 100%、中心误差 0.34px**。坑见 B16（灯条长轴取错对角线；中心受 boxPoints 内外边影响）。S2 板端移植待做。
- 面试复盘文档 `Host_Lichee_RV_Nano/interview_notes.md`（按项目阶段组织的 Q&A）；知识点速查 `edge_ai_cheatsheet.md`。
- **闭环系列（台架，2026-10-10）**：`14_green_fly_smooth`（EMA 平滑+丢检滞回+FC 遥测回读）、`15_closedloop_overlay`（RTSP 上叠加 检测框/offset/RC/FC姿态）、`16_impact_detect`（`RAW_IMU` 加速度突增撞击，敲击实测峰值 5.35g）、`17_task_state_machine`（干跑状态机 IDLE→…→DONE，视觉/IMU 撞击，**相机 pitch 偏置参数**）。

**下位机** `Low_MCU/`
- AT32 方案**已冻结**（`archive_at32/agent_at32.md`）。
- 新主控 = **BetaFPV G473（STM32G474）**，与烧毁的同款**已重购**；**自研移植 INAV 10.0.0（STM32G4）当前能飞**；**本土化已落地**（上位机 MSP UART2@230400 链路已打通；CRSF/光流接口已配）。**2026-10-10 解决"离地即翻/自旋"**（根因＝**电机输出编号与物理接线错位**；修复：mixer 轮换版 + `motor_direction_inverted=OFF` + `align_board_roll=1800`）。工作区 `new_mcu/drone/`，详见 `new_mcu/README.md` 与 `new_mcu/AGENT_GUIDE.md`。

## 6. 技术要点速查

- MSP：`MSP_SET_RAW_RC`(200,≥5Hz)、`MSP_RAW_IMU`(102)、`MSP_ATTITUDE`(108)、`MSP_RX_MAP`、`MSP2_INAV_STATUS`、`MSP2_INAV_ESTIMATED_POSITION`；`MSP RC Override` 掩码 `msp_override_channels`。
- 相机：CVI MMF(VI→ISP→VPSS→VENC)；**编译须用 scpcom 公有头 + 链接板上 scpcom 库**（`/mnt/system/usr/lib` + `/3rd`；另需 `-lgdc`）；抓帧先丢前导帧；sensor_cfg GC4653 beta（lane 4,3,2, mclk1；S02config 决定）；ION/carveout 媒体预留 ~128MB（Form A 正常退出可回收，实测 used=0；异常后仍建议干净重启）。
- RTSP：`experiments/2026-09-26_05_rtsp_stream/`（VI→VENC H265→`rtsp_send_memory_data`）；`rtsp_server_init` **须显式绑 IP**（`RTSP_IP=10.222.2.1`，否则自动绑 wlan0 → USB 连不上，B11）；scpcom 未开放 h264 init；退出 SIGTERM 勿 `kill -9`（否则 ION 泄漏）。
- 绿色检测：opencv-mobile(OpenCV5)。**HSV 标度**：PIL H0-255 vs OpenCV H0-179（`Hcv=Hpil*179/255`）；`findContours/contourArea/moments` 在 **geometry** 模块（`#include <opencv2/geometry.hpp>` + `-lopencv_geometry`）；**无 imgcodecs** → 写 BMP；小目标 `min_area` 要小（B12）。
- NPU：SG2002 代号 cv181x，仅 INT8/BF16；转换链 PyTorch→ONNX→MLIR→INT8 cvimodel；参考 ret7020 YoloCamera 与 MilkV Duo TDL 文档。
- 光流 MTF-02P 协议与 INAV 兼容性未知。

## 7. 给后续 Agent 的要求

- 改动模块前：先读对应指南（`Host_Lichee_RV_Nano/AGENT_GUIDE.md`；下位机历史见 `Low_MCU/archive_at32/agent_at32.md`）。
- 不重复造轮子：优先官方 SDK 示例（TDL/YoloCamera/MilkV Duo）、已有库（opencv-mobile）。
- 上位机结构约定：实验档案制 `experiments/<日期>_<编号>_<描述>/`（自包含、不覆盖）；未来功能各自开新实验（MSP 链路 / 视觉检测 / 控制 / 任务）。任务逻辑与协议编码解耦。
- 测试永远渐进：拔桨 → 绑绳 → 短飞；电机解锁前占空比=0；错误状态必须禁能电机。
- 只做被明确要求的改动，不擅自扩大范围。
- **维护文档**：推进/方案变更时，同步更新本文件 + `README.md` + 各 `AGENT_GUIDE.md` + `difficulty_and_method.md`。

## 8. 风险与待确认项（按优先级）

1. ~~**自写程序黑帧**（中间件版本不匹配）~~ → ✅ **已解决**：`experiments/…/04_vision_module` Form A 出真图，`05_rtsp_stream` 出 RTSP（B7 版本混用 + B9 首帧黑 + B11 RTSP 绑定均已解）。
2. ION 泄漏：Form A/RTSP 正常退出（SIGTERM）可回收（实测 used=0）；**`kill -9` 会泄漏**，需干净重启；程序清理路径在异常分支仍待加固。
3. **不需要**构建 scpcom 的 kernel/middleware（app 只要公有头 + 板上库）。
4. NPU INT8 对自定义目标精度（HSV 兜底）；**缺 TDL SDK**。
5. 下位机 G473：**上位机 MSP 链路已打通**；**"翻/自旋"已解决（电机编号错位）**；当前等电池做**短悬停自稳验证 / PID**；**光流 MTF-02P 待验证**。
6. 撞击检测采样率受限 → 与视觉联合判定。
