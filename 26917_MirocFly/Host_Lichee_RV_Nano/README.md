# MirocFly 上位机（LicheeRV Nano / SG2002）

> 本目录是 MirocFly 自主撞击无人机的**上位机（大脑）**开发区：负责 **采集 → 识别 → 目标选择 → 任务决策**，
> 并通过串口把控制量下发给飞行控制器。下位机（飞控）相关档案在上级目录 `../Low_MCU/`。
>
> 想了解"项目目标 / 架构 / 协议决策"，先读上级 `../README.md` 与 `../agent.md`；本文件侧重**本目录是什么、怎么组织、怎么跑**。

---

## 1. 它是干什么的

- **硬件**：Sipeed LicheeRV-Nano（SG2002，RISC-V C906 + 1TOPS NPU，256MB DDR，MIPI-CSI 摄像头，USB-RNDIS 网络）。
- **在本机系统中的角色**：视觉与决策的"大脑"；下位机只做稳定执行。
- **接口抽象**：上位机内部统一产出 `TargetInfo{偏移X, 偏移Y, 面积Area(∝1/距离²), 状态Status}`；
  链路层只换"编码器"（近期 MSP 摇杆值 / 远期自定义帧），**上层任务逻辑不变，下位机主控更替也不影响上位机**。
- **双重目标**：技术目标（自主撞击不规则物体、30 秒内返航）+ 就业目标（展示「控制 + Linux + 边缘 AI 部署」）。

## 2. 当前进度（一句话）

- **阶段 0 相机出帧**：✅ 完全打通（scpcom 中间件 "Form A"：scpcom 公有头编译 + 链接板上 scpcom 库）。
- **阶段 1 绿色荧光（过渡）**：🟡 感知（HSV 绿色检测）+ 链路（MSP 注入）**都已单独打通**；**视觉→控制闭环**已写好程序，真机联调待下位机到位。
- **阶段 2 不规则物体（YOLO→NPU）**、**阶段 3 下位机自研固件**：未开始。

## 3. 目录导览（一项一项说）

**文档类**
- **`README.md`**：本文件——上位机目录的总览与目录说明。
- **`AGENT_GUIDE.md`**：上位机开发指导（职责、阶段路线图、技术底座、通信协议、代码结构、实况、风险）。动手改代码前应先读。
- **`difficulty_and_method.md`**：**困难与解决办法**记录（编号 A 工具链 / B 相机链路 / C 内存 / D 硬件误判 / E 远程 / F 刷机 / G 命令速查 / H 待办），用于复盘与面试。
- **`diary.md`**：**上位机操作手册**——如何在本目录编译/部署/运行各功能模块（相机取帧、RTSP、绿色检测、MSP 自测、闭环程序）与排错、板子命令速查。

**实验档案 `experiments/`（核心工作区）**
- 约定：**每个技术尝试一个独立文件夹**（`日期_编号_描述`），自包含、不覆盖、不删除；`experiments/README.md` 是索引表。
- `2026-09-24_01_lowlevel_mmf/`：早期"低层 CVI MMF 直接抓帧"（板上跑不通，弃用存档）；含通用小工具 `nv21_to_bmp.py`、`detect_green.py`（HSV 初版）。
- `2026-09-26_02_sipeed_middleware/`：自写中间件链 sipeed 版 → 全黑（"版本混用"反例，弃用）。
- `2026-09-26_03_scpcom_formA/`：**Form A 首次隔离实验**，证明"scpcom 源码中间件 + 板上 scpcom 库"能出真图（存档）。
- `2026-09-26_04_vision_module/`：**当前相机层**——`camera.h/c` 可复用取帧 API + `frame_grab` CLI（丢前导黑帧、输出自动清理、朝向开关）。
- `2026-09-26_05_rtsp_stream/`：**RTSP 实时预览**——VI 取帧 → VENC(H265) → RTSP 推流（复用 scpcom `rtsp_server/` + `media_server` 静态库）。
- `2026-09-26_06_hsv_green/`：**HSV 绿色检测**（opencv-mobile）——输出目标 `offset/area`，PC 调参脚本 + 板端程序 + BMP 可视化。
- `2026-09-26_07_msp_link/`：**MSP 链路**——MSP v1 编解码/解析/串口/`offset→RC`；用 PTY 模拟 FC 免硬件自测。
- `2026-09-26_08_vision_control/`：**"识别绿色并飞过去"**——06 检测 + 07 链路合成（PTY 假 FC / 真串口两用），阶段 1 闭环雏形。

**共享资源（放在本目录顶层，供各实验复用）**
- `board_libs/`：从板子拉下来的 **scpcom `.so` 链接副本**（供交叉编译链接，**不部署回板**；运行时用板上 `/mnt/system/usr/lib`）。**不入库**。
- `fetch_board_libs.sh`：一键从板子把上述 `.so` 拉到 `board_libs/`。
- `.gitignore`：忽略 `board_libs/` 与编译/抓拍产物。

**外部 SDK / 参考（只读，一般不要构建）**
- `LicheeSG-Nano-Build_scpcom/`：**scpcom 版整套 SDK 源码树**（与板上镜像同源）。我们的相机/RTSP 代码取自其 `middleware/sample/test_mmf/`；`tdl_sdk/` 是阶段 2 的 NPU 高层 SDK。**不要整树构建**（见 `difficulty_and_method.md` A5）。
- `LicheeRV-Nano-Build_official/`：官方 SDK。主要借两样东西——`host-tools/gcc/riscv64-linux-musl-x86_64/`（**交叉编译工具链**，含 sysroot）和 `linux_5.10/.../riscv/usr/include`（**内核头**）。
- `opencv-mobile-5.0.0-licheerv-nano/`：预编译轻量 OpenCV（静态库 + `include/opencv5`）。检测程序链接它；注意**无 imgcodecs/videoio**，OpenCV5 的轮廓函数在 `geometry` 模块。
- `CVI_HW_OpenCV/`：把硬件加速接进 OpenCV 的参考仓库（含抓帧 patch、工具链 cmake），参考资料，未直接使用。
- `test_camera.cpp`：早期 `cv::VideoCapture(0)` 试错文件（行不通，留作教训）。

**硬件资料**
- `LicheeRV_Nano-70415_Schematic.pdf`：板子原理图。
- `LicheeRV_Nano_v70405_specification_V1.0_en.pdf`：板子规格书。

## 4. 快速开始（以相机层与检测为例）

```bash
# 0) 首次: 拉板上库作链接输入
./fetch_board_libs.sh

# 1) 相机取帧（实验 04）
cd experiments/2026-09-26_04_vision_module
make
scp frame_grab root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1 'cd /root/mirocfly && \
  (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./frame_grab 1280 720 1 10 5 >/tmp/fg.log 2>&1 &); \
  sleep 12; killall frame_grab'
scp root@10.222.2.1:/root/mirocfly/cam_1280x720.raw .
python3 ../2026-09-24_01_lowlevel_mmf/nv21_to_bmp.py cam_1280x720.raw 1280 720 out.bmp

# 2) 绿色识别并飞向目标（实验 08, 离线 PTY 假 FC）
cd experiments/2026-09-26_08_vision_control
make
scp green_fly root@10.222.2.1:/root/mirocfly/
ssh root@10.222.2.1 'cd /root/mirocfly && \
  (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./green_fly 1280 720 150 >/tmp/gf.log 2>&1 &); \
  sleep 20; killall green_fly; grep -aE "FOUND|neutral|recv" /tmp/gf.log'
```
> 板上没有 `timeout` 命令，用"后台 + `sleep` + `killall`"。停止相机/推流类程序用 **SIGTERM**（`killall`），**不要 `kill -9`**，否则泄漏 ION。

## 5. 开发约定（重要）

- **实验档案制**：新尝试开新文件夹（`experiments/日期_编号_描述/`），写自己的 `README.md`，不覆盖旧实验。
- **相机/中间件**：自写程序必须用 **scpcom 公有头**编译、**链接板上 scpcom 库**；**禁止混用 sipeed/官方中间件**（会取帧全黑）。
- **不构建 scpcom 整树**：app 只需"公有头 + 板上库"；编中间件内部模块才需要内核 uapi，那是牛角尖。
- **运行环境**：`LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`；链接期注意 `-lgdc`（板载 `libvi/libvpss` 依赖）。
- **ION 内存**：反复运行后若异常，`reboot` 干净重启；正常退出（SIGTERM）会回收，`kill -9` 会泄漏。
- **测试永远渐进**：拔桨 → 绑绳 → 短飞；电机解锁前占空比=0；错误状态必须禁能电机。
- **文档同步**：推进/方案变更时更新 `AGENT_GUIDE.md`、`difficulty_and_method.md`、上级 `README.md`/`agent.md`。

## 6. 相关文档

- 项目总览 / AI 上下文：`../README.md`、`../agent.md`
- 上位机开发指导：`./AGENT_GUIDE.md`
- 困难与解决（复盘/面试）：`./difficulty_and_method.md`
- 实验索引：`./experiments/README.md`
- 操作手册（编译/部署/运行/排错）：`./diary.md`
- 下位机经验存档：`../Low_MCU/archive_at32/agent_at32.md`
