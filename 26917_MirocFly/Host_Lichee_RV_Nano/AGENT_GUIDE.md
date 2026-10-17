> ⭐ **【第一要务 · 教学优先】** 所有的开发都是以学习为目的，让我学会东西为前提，而并非把目标完成放在第一位，我要穷究原理，哪怕工期可能延误，我会多抽出其他时间，所以你要以教学和传授方法知识为第一要务。
> 📖 **开工前必读**：`../../agent问答示例.md` —— 先学习其中的逻辑与思路（要做什么 → 为什么 → 由谁做基础），并作为**贯穿始终的回答方式**。

# AGENT_GUIDE — 上位机（LicheeRV-Nano / SG2002）开发指导

> 本文件专供后续 AI Agent 在**上位机**文件夹内工作时使用。请先读完本文件，再动手改代码。
> 困难与解决办法的详细记录见同目录 `difficulty_and_method.md`。

## 1. 项目总体方向（双重目标）

- **技术目标**：自主撞击不规则物体，30 秒内返航。
- **就业目标**：一次项目同时展示「控制 + Linux + 边缘AI部署」复合能力。
- **原则**：就业价值来自任务刚需本身，不是"加戏"——识别不规则物体天然要求边缘AI部署。

## 2. 上位机在系统中的职责（全部开发工作量所在）

```
LicheeRV Nano (SG2002) —— Linux视觉层: 采集→识别→目标选择→任务决策
        │  近期: MSP (SET_RAW_RC 注入 + RAW_IMU/ATTITUDE/STATUS 回读)
        │  远期: 0xAA 0x55 | X:int16 | Y:int16 | Area:uint16 | Status:uint8 | CRC8
        ▼
下位机 BetaFPV G473 (STM32G474) —— 近期: INAV 10.0.0(自移植, 已能飞)  远期: 自研飞控
（⚠️ 下位机硬件已更替：原 AT32 直驱空心杯升力不足（59g<76g）→ 新主控 BetaFPV G473
   (STM32G474)，与烧毁的同款已重购；自移植 INAV 10.0.0(G4) 能飞，但"尚未本土化"
   —— 上位机 MSP / 光流 / 接口映射未定，旧接口约定不再默认成立。
   详见 Low_MCU/new_mcu/README.md）
```

**关键接口抽象**：上位机内部统一产出 `TargetInfo{像素偏移X, 像素偏移Y, 面积Area(∝1/距离²), 状态Status}`，链路层只换"编码器"（近期 MSP 摇杆值 / 远期自定义帧）。**上层任务逻辑两阶段零改动，且下位机主控更替也不影响上位机。**

## 3. 阶段路线图与当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| **0 相机出帧** | 交叉编译取帧程序 → CSI 抓帧存 NV21；获 `VIDEO_FRAME_INFO_S` | 🟢 **打通（Form A：`experiments/…/04_vision_module`）**；`05_rtsp_stream` RTSP 预览通 |
| **1 绿色荧光（过渡）** | HSV 检测荧光绿 → 多目标选择 → 像素偏移→摇杆修正（ANGLE）→ MSP 注入 INAV | 🟢 **台架闭环全通**：检测(06)+链路(07)+打磨(14 EMA/丢检滞回/FC遥测)+可视化(15)+撞击(16)+状态机干跑(17)；真机联调待电池 |
| **2 不规则物体（最终）** | YOLOv8n 自训 → INT8 cvimodel → TDL SDK 部署 NPU → 撞击 → 自稳 → 30s 返航 | 🟡 **NPU 部署通路已通**（09：官方 YOLOv8n INT8 检测 17.6 FPS）；自训/量化待做 |
| **3 自写固件（远期）** | 下位机自研飞控（原 AT32 计划；新主控 G473 已重购、INAV G4 能飞，待对接后再推进） | ⏳ 远期 |

**阶段 0 结论（重要，已更正）**：
- ✅ **相机硬件正常**（模组/排线/传感器都没坏）。
- ❌ **Sipeed 官方镜像里 GC4653 中间件是坏的**（vendor 自带 `test_mmf`/`sensor_test` 同样挂）。
- ✅ **scpcom 镜像 + scpcom 中间件可用**：板上 `/mnt/system/usr/bin/test_mmf 4` 出真实画面。注意：**scpcom 版 `_test_venc_jpg` 走相机**（`mmf_vi_frame_pop`），sipeed 版走合成彩条——别混淆。
- ✅ **自写程序已出真图（Form A，2026-09-26 验证）**：`experiments/2026-09-26_03_scpcom_formA/` 首次验证，`experiments/2026-09-26_04_vision_module/` 固化。用 **scpcom 公有头**编译 + **链接板上 scpcom 库**（`LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`，不部署自带 .so）。此前黑帧 = **中间件版本混用** + **抓到启动首帧** 两个问题叠加；现抓帧前丢前导帧（默认 10）。
- ⚠️ 曾误判"`sensor_cfg.ini` 被改坏"：实测板上默认即 **beta**（`lane 4,3,2 / mclk1`），我们覆盖的内容 = beta，**并未改坏**。
- **相机光学（GC4653 官方镜头）**：焦距 **3.05mm** / 光圈 F2.5 / **畸变 5%**；FOV **对角 90°、水平 81°、竖直 51°**（1/3" 传感器 2560×1440、2.0µm 像元；有效幅面 5.12×2.88mm）。换算：**1m 处约覆盖 1.6m×0.92m，2m 处 ~3.2m×1.85m**（近似，含 5% 桶形畸变 → 边缘"像素→角度"非线性）。
  - **固定前视安装**建议：**pitch 下倾约 10–15°**（竖直 51° 很宽 → 对角度不敏感；巡航 1–1.5m 时覆盖 ~1.5m~∞）；yaw/roll = 0（光轴对齐机头、水平与机身齐）；**倒装(绕光轴180°)用 `MIROCFLY_CAM_ROT=180` 兜底**；垂直基准用 `pitch_bias_px` 标定（目标放"期望攻击方向"读 `sy` → 设 `pitch_bias_px=sy`）。**换装后必须重跑静态标定**（右→roll>1500、上→pitch>1500）。见 exp20。

## 4. 已确认的技术底座（勿重复造轮子）

- **交叉编译链**：`LicheeRV-Nano-Build/host-tools/gcc/riscv64-linux-musl-x86_64/`（Xuantie GCC 10.2，musl，C906）。
- **相机抓帧走 CVI MMF（不是标准 VideoCapture）**：opencv-mobile 无 videoio/imgcodecs；CSI 经 VI→ISP→VPSS→VENC。抓到的 `VIDEO_FRAME_INFO_S` 正是 YOLO 推理要的格式。
- **`CVI_HW_OpenCV/patches/cvi_frames_read.patch`**：让 opencv-mobile 的 `VideoCapture` 暴露 `cap.image_ptr`（`VIDEO_FRAME_INFO_S*`）。
- **镜像基座**：**scpcom 镜像 + scpcom 中间件**（RNDIS/SSH 正常）；官方镜像相机坏。源码树 `LicheeSG-Nano-Build_scpcom/`（`scpcom/LicheeSG-Nano-Build`，sophgo weekly 2024.10.14 中间件）。**自写程序必须用 scpcom 公有头 + 板上 scpcom 库**，禁止混用 sipeed 中间件。
- **NPU 就绪度**（板上已有）：`/usr/bin/lib/libcvikernel.so`、`libcviruntime.so`、`libcvimath.so`；模型 `yolov5s_224_int8.cvimodel` 等。**缺高层 TDL SDK（`libcvi_tdl`）**，阶段 2 补。
- **边缘AI参考**：`ret7020/LicheeRVNano` 的 `Projects/YoloCamera`（CSI+YOLOv8n+NPU，640×640 约 17–27 FPS）；MilkV Duo 官方 TDL 文档（YOLOv5/v8/v11/v12，同芯片）。模型量化仅 **INT8/BF16**（BF16 YOLO 实测失败），转换链 PyTorch→ONNX→MLIR→INT8 cvimodel。
- **板上访问**：USB-RNDIS 网卡 → `ssh root@10.222.2.1`（换镜像后需重装公钥、`ssh-keygen -R`）。

## 5. 通信协议要点（近期 MSP）

- 下位机 INAV **串口只支持 MSP**（已确认；新板固件为自移植 INAV 10.0.0 / STM32G4）。
- 注入：`MSP_SET_RAW_RC`(200)，≥5Hz，推荐 10Hz。
- 回读：`MSP_RAW_IMU`(102, 撞击检测)、`MSP_ATTITUDE`(108)、`MSP_RX_MAP`、`MSP2_INAV_STATUS`、`MSP2_INAV_ESTIMATED_POSITION`（光流位置）。
- **RC 优先**：INAV `MSP RC Override` 飞行模式（默认启用）+ `msp_override_channels`；飞行员拨杆切换（ch8）。**⚠️ 掩码位序 = 内部通道号 AERT（`ROLL=0 PITCH=1 YAW=2 THROTTLE=3`），不是负载的 AETR**：只接管三轴、油门留飞行员 = **`7`(0111)**（`11`(1011) 会接管油门→用 payload 油门覆盖物理油门→切油！）。见实验 18。
- 参考工具：stronnag/msp_set_rx、INAV Configurator。

## 6. 代码结构与实验约定

**实验档案制**：每个技术尝试一个独立目录，自包含、不覆盖、不删除（索引见 `experiments/README.md`）。
```
Host_Lichee_RV_Nano/
├── experiments/                            # 实验档案
│   ├── README.md                           # 索引表(编号/日期/目的/结论/状态)
│   ├── 2026-09-24_01_lowlevel_mmf/         # 低层 CVI MMF (弃用; 含 nv21_to_bmp.py/detect_green.py)
│   ├── 2026-09-26_02_sipeed_middleware/    # 链 sipeed 中间件 → 黑帧 (弃用)
│   ├── 2026-09-26_03_scpcom_formA/         # Form A 首次隔离实验 (存档)
│   ├── 2026-09-26_04_vision_module/        # ✅相机层: camera.h/c + frame_grab (CLI)
│   ├── 2026-09-26_05_rtsp_stream/          # ✅VI→VENC(H265)→RTSP 实时预览 (rtsp_grab)
│   ├── 2026-09-26_06_hsv_green/            # ✅HSV 绿色检测 (green_detect; 出 offset/area, 存 BMP)
│   ├── 2026-09-26_07_msp_link/             # ✅MSP 链路 (msp.c/msp_test; SET_RAW_RC+RAW_IMU, offset→RC; PTY 自测)
│   ├── 2026-09-26_08_vision_control/       # ✅识别绿色并飞过去 (green_fly; 检测+MSP 闭环; PTY/真串口)
│   ├── 2026-09-26_09_npu_demo/             # ✅NPU 部署 (npu_hello 底层 cviruntime; tdl_detect 高层 TDL 检测)
│   ├── 2026-09-27_10_tdl_infer/            # ✅可复用推理层 (mf/tdl.h; 相机+检测+画框+RTSP)
│   ├── 2026-09-28_11_quantize/             # ✅TPU-MLIR 量化 (ONNX→cv181x INT8 cvimodel)
│   └── 2026-09-29_12_armor_led/            # 🟡传统CV装甲板 (红/蓝LED, 通道差+几何配对; PC cv2 原型)
├── board_libs/                             # 板上 scpcom .so 链接副本 (gitignore; fetch_board_libs.sh)
├── LicheeSG-Nano-Build_scpcom/             # scpcom 源码树(中间件/样例/tdl_sdk)
└── LicheeRV-Nano-Build_official/           # 官方工具链 + 内核头
```
- 待建功能（未来实验）：MSP 链路 / 视觉检测(HSV→YOLO) / 控制 / 任务状态机 / 主程序。
- 共享库**不部署**；运行时用板上 `/mnt/system/usr/lib[:/3rd]`。任务逻辑与协议编码解耦。

## 7. 当前工程实况

- **镜像**：scpcom 构建（`/boot/ver`≈`6f9596`，内核 `2026-07-14`，中间件 `libsys 87be70b`）；源码树 `LicheeSG-Nano-Build_scpcom/`；官方镜像相机坏。
- **`experiments/2026-09-26_04_vision_module/`（✅相机层）**：Form A。`camera.h` API + `frame_grab` CLI；编译 scpcom `maix_mmf/sophgo_middleware.c`，链接 `board_libs`（`-lsample` 等 + 必须 `-lgdc`）。`mf_camera_warmup(10)` 丢前导黑帧；输出自动保留最新 N 个。运行：`LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./frame_grab 1280 720 1 10 5`。
- **`experiments/2026-09-26_05_rtsp_stream/`（✅RTSP）**：VI→VENC(H265)→RTSP，复用 scpcom `rtsp_server/` + `media_server` 静态库。**必须显式绑 IP**（`RTSP_IP=10.222.2.1`，否则自动绑 wlan0，见 B11）；仅 H265；退出用 SIGTERM。
- **`experiments/2026-09-26_06_hsv_green/`（✅绿色检测）**：PC 调参（`pc_tune/detect_green.py`）+ 板端 `green_detect.cpp`（opencv-mobile）。VI→NV21→BGR→HSV→最大轮廓→`offset/area`→存 `green.bmp`。阈值 PIL H85-130/S40-170/V55-170（OpenCV H60-91）；OpenCV5 的轮廓函数在 **geometry** 模块；**无 imgcodecs** 故写 BMP。运行：`LD_LIBRARY_PATH=… ./green_detect 1280 720 60`。
- **`experiments/2026-09-26_07_msp_link/`（✅MSP 链路）**：纯 C（`msp.c`）。MSP v1（csum=异或）编解码/流式解析/串口/`offset→RC`。**无硬件自测**：`./msp_test`（selftest + **PTY 模拟 FC** 端到端）。真硬件：`./msp_test inject /dev/ttyS1 460800 10`（注入）/ `monitor`（回读）。
- **`experiments/2026-09-26_08_vision_control/`（✅识别绿色并飞过去）**：06 检测 + 07 链路合成。`green_fly`：检测 offset → `msp_offset_to_rc` → `SET_RAW_RC`；默认 **PTY 假 FC**（无硬件可跑），给串口参数即注入真 FC。安全：无目标回中、油门默认 1000。
- **`experiments/2026-09-26_09_npu_demo/`（✅NPU 部署入门）**：步骤1 `npu_hello` 底层 **cviruntime**（mobilenet 226 FPS）；步骤2 `tdl_detect` 高层 **TDL** 图片检测（官方 `yolov8n_det_coco80_640` INT8，**17.6 FPS**）；步骤3 `vi_detect` **相机实时检测**（TDL 内部 VI，1280×720，**16.4 FPS**）。链接：需链 `board_libs` + `-lgcc_s`；**不链 `-ltdl_ex`**（拉 libcurl，板上 libssl 不匹配，见 B13）；编译 `sample_utils.cpp` 还需 nlohmann/json + OpenCV 头。运行需 `LD_LIBRARY_PATH` 含 `/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/system/lib:/usr/bin/lib:/maixapp/lib:/mnt/system/opt/cvitek_tpu_sdk/lib`。
- **`experiments/2026-09-27_10_tdl_infer/`（✅可复用推理层）**：`include/mf/tdl.h`(C 接口) + `src/mf_tdl.cpp`（相机/TDL检测/自绘框/RTSP）。`tdl_app` 相机实时检测 + 画框 + RTSP 叠加：`rtsp://10.222.2.1:554/h265`（**H265** 1280×720，~16 FPS）。**坑见 B14**：`TDL_WrapImage` 传**帧指针**（`VIDEO_FRAME_INFO_S *frame=NULL; TDL_WrapImage(image,&frame)`），传结构体会导致 RTSP 花屏。
- **`experiments/2026-09-28_11_quantize/`（✅TPU-MLIR 量化）**：**在 Linux i5** 上用 TPU-MLIR docker 把 ONNX 量化成 cv181x INT8 `cvimodel`。环境：镜像走**算能国内 CDN**（`tpuc_dev_v3.4.tar.gz` 2.12GB）→ `docker load`；容器内 `pip install tpu_mlir` 后 commit。三段式：`model_transform.py → run_calibration.py → model_deploy.py --quantize INT8 --processor cv181x`。实测 `resnet18`→cvimodel，板上 `npu_hello` **43.9 FPS**。**坑见 B15**（Docker Hub 慢/内容存储被中断拉取污染/bridge 网络坏用 `--network none`/镜像缺 tpu_mlir）。
- **`experiments/2026-09-29_12_armor_led/`（🟡传统CV装甲板 S0+S1）**：不依赖训练的**并行兜底/预研**。S0 `pc_tune/gen_synth_armor.py`（**纯 PIL** + 自解 8×8 单应）生成**透视梯形**合成图 21 张 + 真值 `synth_armor/labels.csv`；S1 `pc_tune/detect_armor.py`（**cv2**；PC 独立 venv `~/.venvs/mirocfly` 装 `opencv-python 5.0.0`，**与板端 opencv-mobile 5.0.0 对齐**）：通道差(`红=R−max(G,B)`/`蓝=B−max(R,G)`)→形态学→`minAreaRect`→同色**松弛配对**→**四点四边形对角线交点**为中心。合成集 **召回/精确 100%、中心误差 0.34px**。坑见 B16。S2 板端移植待做。
- **闭环系列（台架, 2026-10-10）**：`experiments/2026-10-10_14_green_fly_smooth`（偏移 EMA 平滑+丢检滞回+FC 遥测回读）、`…/15_closedloop_overlay`（RTSP 上叠加 检测框/offset/RC/FC姿态）、`…/16_impact_detect`（`RAW_IMU` 加速度突增撞击，敲击实测峰值 5.35g）、`…/17_task_state_machine`（干跑状态机 IDLE→…→DONE + 视觉/IMU 撞击 + **相机 pitch 偏置参数 `pitch_bias_px`**）。
- `experiments/2026-10-11_18_msp_takeover/`（🟡**上位机操控下位机**）：`takeover` 两模式——`track`（HSV 绿目标→EMA/滞回→roll/pitch→`SET_RAW_RC`，经 FC `MSP RC Override` 接管）+ `probe`（**方向自检**：脚本化发 roll±/pitch±，回读 `MSP_MOTOR`/`MSP_ATTITUDE`/`MSP_RC`，按相对 neutral 的 Δ 定符号/kp/是否真接管）。**纠正 INAV `msp_override_channels` 位序=AERT**：接管三轴=**7**（FC 已设 7 并 `save`）。**台架验证通过**：接管生效（`MSP_RC` 回读吻合）、油门不被覆盖、混控 Δ 与 `mmix` 一致、符号自洽；**开放问题：水平误差用 roll 还是 yaw（待定）**；真机待标定。
- **`experiments/2026-10-12_19_opflow_hold/`（🟡光流水平阻尼）**：`flow_damp`——**新增 MSP v2 收发**（`msp2_send_request`/`msp_parser2_feed`，吃 v1+v2 帧；v2 帧 `$X` flags cmd(2) size(2) payload crc8_dvb_s2）+ `mon`（只读 `MSP2_INAV_OPTICAL_FLOW`(0x2001)/`MSP_SONAR_ALTITUDE`(58)）+ `damp`（光流估水平速度→roll/pitch 阻尼，依 FC `navigation_pos_estimator_flow.c` 约定：`v_右=+(flowX-bodyX)·(π/180)·alt`、`v_前=-(flowY-bodyY)·(...)`）。数据通路已验（`q≈40–60`，质量一般）。
- **`experiments/2026-10-12_20_paradigm_select/`（🟡接管三轴+范式/定点/定高选择）**：`paradigm`——读 `MSP_RC` 取 ch6/ch7；`ovr=(ch6>1300)||(ch8>1300)`；`ch6>1300`→**范式B**（roll 追踪，`pitch_bias_px` 作垂直基准）；`ch7[1300,1700)`→**定点**（光流阻尼+位置积分，进入清零锚点+泄漏）、`[1700,2100)`→**定高（桩，需 mask=15）**；**接管期间始终叠加基础光流阻尼**。健壮性：`MIROCFLY_CAM_ROT=0|180`（相机倒装冗余，检测前 `cv::rotate`）、`alt` 无效值(0xFFFFFFFF/异常)回退兜底高度、光流无效时阻尼/积分归零。`mon`/`dry` 已验证（开关读值/模式判定/视觉符号自洽），**待台架真机**。预启动 `run/stop_paradigm.sh`。
- **面试复盘**：`interview_notes.md`（按阶段 Q&A）+ `edge_ai_cheatsheet.md`（速查）。
- **实验 01/02/03（旧/存档）**：低层 MMF、sipeed 链、Form A 首次实验，均不再作为正解。
- `sensor_cfg.ini`：GC4653 **beta**（= 板默认）；由镜像 `/etc/init.d/S02config` 选 alpha/beta + 设 pinmux。
- **工具链**：`LicheeRV-Nano-Build_official/host-tools/gcc/riscv64-linux-musl-x86_64/`。
- `nv21_to_bmp.py`（PC 端 NV21→BMP）、`detect_green.py`（HSV）在实验 01 目录；`CVI_HW_OpenCV/`、`opencv-mobile-5.0.0-licheerv-nano/` 就绪。

## 8. 已知风险与待解决项

1. ~~**自写程序黑帧**~~ → ✅ **已解决**：`experiments/.../04_vision_module` Form A 出真图（B7 版本混用 + B9 首帧黑）。
2. **ION 泄漏**：反复运行会耗尽 carveout → 每实验前干净重启；程序清理待修。
3. **不需要**构建 scpcom 的 kernel/middleware（app 只要公有头 + 板上库）。
4. **TDL SDK 缺失** → 阶段 2 补（YOLO）。
5. NPU INT8 对自定义目标精度 → 保留 HSV 兜底。
6. 撞击检测用 `MSP_RAW_IMU` 轮询（采样率受限）→ 与视觉"bbox 达阈值"联合判定。
7. **定高/定点改为下位机固件实现（上位机定高冻结，2026-10-14 定；10-15 修订：定高优先、定点条件重开）**：无气压计时 INAV 的 `est.pos.z` **没有测距来源**（测距只进 AGL 估计，且 AGL 还要求 `EST_BARO_VALID`），故 ALTHOLD/POSHOLD 无法准入。**决定：在固件加“测距 Z 分支”让二者可用**（见 `../Low_MCU/new_mcu/AGENT_GUIDE.md` §5.6）。**上位机侧自算定高（接管油门做高度环）冻结、不做**。**POSHOLD 另有无 GPS 解锁拦截问题（见下），故一期只做 ALTHOLD，定点以“光流/地面测试”结论为门。**
8. **已到 `exp20`（接管三轴+范式/定点/定高选择）**。⚠️ **2026-10-14 调整接管方案**：发射器只留 **`ch5`=锁桨 + `ch8`=接管**；`ch8` 高区间启用 `OVERRIDE+ANGLE+ALTHOLD`（**POSHOLD 视光流结论再定**）；**接管分两步**——**一期**只接管 roll/pitch/yaw（`mask=7`，飞行员持油门），**二期**再接管油门（`mask=15`）。失效统一 `LAND`（慢降），**不返航**。详见根 `agent.md` §3.5。旧的 ch6/ch7（范式/定点/定高选择）方案**作废**。**下一步（2026-10-15 起）**：① **阶段 0：光流/地面测试**——用 `paradigm mon` 在**光滑地板 vs 有纹理不反光地垫**、不同光照/高度下测 `q` 与流方向，并用尺子对比测距误差（结论决定是否重开 POSHOLD）；② 台架验三轴接管（追踪/阻尼、调 `kpvis/kd/sx/sy`）；③ 真机短飞；④ 固件 Z 分支与定高（见 `Low_MCU` 指南）。相机固定前视：**pitch 下倾 ~10–15°**、倒装用 `CAM_ROT=180`、换装后重跑静态标定并标 `pitch_bias_px`。视觉改 YOLO（阶段2）时本层只换检测源。
9. **无 GPS 时配置 POSHOLD/RTH/WP 会拦截解锁（2026-10-15 核实）**：`isUsingNAVModes` 含 POSHOLD/RTH/WP 等（不含 ALTHOLD）→ 解锁要求 `STATE(GPS_FIX_HOME)`，无 GPS 时拒绝解锁。故“先定高、定点后置”。另 `eph` 仅在光流更新超时时增长 → 光流健康是定点前提。详见 `../Low_MCU/new_mcu/AGENT_GUIDE.md` §6-19。
10. **看门狗（纳入计划）**：FC 已有隐式链路看门狗（MSP 超时→failsafe），但上位机内部假死可能仍发陈旧指令 → 计划加“控制循环心跳+监督线程，超时停发 RC”；**无人飞行前必做**。
11. **阶段 0 结论（2026-10-15，只读 `paradigm mon` 实测，已定论）**：**光流质量由“表面纹理”主导，与高度基本无关**。
    - 光滑地面：`q≈20–24`（固定架 38/50cm、手持 70cm 均如此）→ **低于 INAV 门限，光流不启用**。
    - 有纹理面（鼠标垫/纹理垫）：`q≈37–53`（29–61cm）→ **≥35，光流可启用**（同架同静止，只换面：21→39）。
    - 强光直射反而更差（`q≈20`，疑高光过曝）；q 会随运行时间缓慢上升（上电后等数秒更稳）。
    - **判定口径（源码核实）**：上位机 `q` 为 0–100 标度（模块字节×100/255）；`sensors/opflow.c` 门限 `SQUAL_HIGH=35`（≥35 才进入 VALID）、`SQUAL_LOW=10`（≤10 才退出，滞回）。
    - **操作性结论**：飞行/测试区铺**有纹理不反光地垫（鼠标垫即可）**，飞行高度 0.3–0.6m；上电后稍等数秒再起飞。→ **POSHOLD 的“光流健康”前提在该条件下可满足**（仍受无 GPS 解锁拦截限制，见 §6-19）。
    - **⚠️ 观察**：ch6/ch8 中位即 1500，而接管区间是 `>1300` → **中位也算“接管激活”**（当前 `ovr=1`），单开关方案需改为两段开关或把区间收到 `[1700,2100)`。

12. **定高首飞与黑匣子定论（2026-10-17，用户试飞）**：Z 分支固件在飞；黑匣子 `~/Downloads/第一次定高.TXT` 证实 **`ANGLE｜NAV_ALTHOLD` 真实生效**（`flightModeFlags=10`）、**高度估计可信**（`navFlags bit0=1`，Z 分支在飞行中给出 TRUSTED 高度）、爬升 ~1.66m。**问题**：① 手感反馈**水平漂**（黑匣子里电机输出长期不对称，m0 恒低 ~50，疑 CoG/校准 + ANGLE 无位置环固有）；② **灵敏度高（轻打杆大幅动作）**；③ **段4 结尾 ~14.4s 翻滚坠机**（roll→70°、m1 饱和 2000、油门命令顶满但高度反降，`navState` 1→3，已归因见段末"10-17 晚更新"）。`mon` 扩展字段（z/vz/R/P/Y/T）已在本次验证中实用（注意未解锁 z 恒 0）。**10-17 晚更新**：坠机已归因（`nav_mc_hover_thr` 未标定）+ 修复（hover_thr=1585、`inav_max_surface_altitude` 200→400cm）+ 拨入瞬态复测通过；失效核实：拔上位机串口只回落物理遥控、不 LAND（详见 `../Low_MCU/new_mcu/AGENT_GUIDE.md` §5.7）。**深夜更新**：光流遥测符号 bug 已修（新固件 `7e2d65a5`）+ `align_opflow=CW90` 定标验证（右→+X、前→−Y）；⚠️ **exp19/20 的阻尼参数（kd/sx/sy）曾在丢符号数据上标定，需复核**；`opflow_scale` 待精标（粗估 ≈2.5）。

> 维护提醒：每次推进/方案变更，同步更新本文件 + 根 `README.md` / `agent.md` + `difficulty_and_method.md`。
