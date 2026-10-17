> ⭐ **【第一要务 · 教学优先】** 所有的开发都是以学习为目的，让我学会东西为前提，而并非把目标完成放在第一位，我要穷究原理，哪怕工期可能延误，我会多抽出其他时间，所以你要以教学和传授方法知识为第一要务。
> 📖 **开工前必读**：`../agent问答示例.md` —— 先学习其中的逻辑与思路（要做什么 → 为什么 → 由谁做基础），并作为**贯穿始终的回答方式**。
> 🎯 **【当前唯一小目标 · 2026-10-17 定】** 先把**下位机调稳**（稳定悬停 / 定高可用） → 再实现**上位机视觉跟踪绿色目标**（HSV 绿 → 接管闭环）。其他一切（返航、撞击、规则合规、优化项）**暂缓**；讨论与执行都紧扣这条主线，不展开无关内容。

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
| 下位机策略 | 近期 **INAV（允许改固件：加测距 Z 分支以实现定高定点）**；远期自研飞控。⚠️ **2026-10-14 起不再拘泥“只配置不改固件”**（见 §3.5） |
| **下位机硬件** | **BetaFPV G473（STM32G474，无刷 + 四合一 ESC）**，与烧毁的同款已重购；自移植 INAV 10.0.0(G4) 能飞（原 AT32 直驱空心杯升力不足被淘汰） |
| 通信协议 | 近期 **MSP**（INAV 串口仅支持 MSP）；远期自定义帧 `0xAA 0x55` |
| INAV 固件 | 新板 **自移植 INAV 10.0.0（STM32G4，可重编）**；旧 AT32 板的 INAV 9.0.0 为不可重编的预编译版（随 AT32 淘汰） |
| RC 优先 | INAV `MSP RC Override` 模式（`BOX_MSP_RC_OVERRIDE` + `msp_override_channels`，**需 CLI 配置、默认不覆盖**）；飞行员拨杆切换 |
| 视觉方案 | 阶段1 绿色荧光 HSV（过渡）；阶段2 不规则物体 **YOLOv8n→INT8→TDL SDK→NPU**；HSV 仅兜底 |
| **相机方案** | **用 scpcom 镜像 + scpcom 中间件**（官方镜像 GC4653 中间件坏；相机硬件正常）。自写程序须用 scpcom 公有头编译 + 链接板上 scpcom 库，**禁止混用 sipeed/官方中间件**（混用→取帧全黑） |
| 撞击检测 | 近期上位机轮询 `MSP_RAW_IMU`(102) + 视觉 bbox 阈值；远期下位机 IMU 检测 |
| 返航 | **近期不做返航**；失效统一 `LAND`（慢降）。远期厘米级返航由**外挂 FPGA** 提供定位（接口未定义，单独立项，见 §3.5） |
| 任务预算 | 30s：接近~8s / 撞击自稳~5s / 返航~15s |

## 3.5 全自动任务目标与接管分期（2026-10-14 定稿）

**最终目标**：从起飞到降落**全自动，人只负责开机**。飞行中：上位机出视觉/决策与三轴（后期含油门）指令；下位机负责姿态稳定、定高、定点。人工输入仅剩两个物理开关：`ch5`=锁桨、`ch8`=接管。

**接管分期（两步走，勿跳步）**

| 期 | 内容 | `msp_override_channels` | 固件 |
|---|---|---|---|
| **一期（近期）** | 上位机接管 **roll/pitch/yaw**（ANGLE 模式），飞行员仍持油门 | `7` | 不改 |
| **二期（一期成熟后）** | 上位机一并**接管油门** + **固件定高定点** | `15` | 新增测距 Z 分支 |

**为什么定高定点必须放二期**：ALTHOLD 用油门通道作“爬升/下降速率”参考（`navigation_multicopter.c`）；发射器无油门摇杆时该通道停在低位 → 判为下降 → 定高失效。故定高定点必须以“接管油门”为前提。

**关键决策（2026-10-14）**
- **定高定点改为下位机固件实现**（**上位机定高冻结**）。原因：无气压计、无 GPS 时 INAV 的 `est.pos.z` 无测距来源，ALTHOLD/POSHOLD 无法准入；只在固件加“测距 Z 分支”才能让二者可用（详见 `Low_MCU/new_mcu/AGENT_GUIDE.md`）。
- **发射器只留两通道**：`ch5`=锁桨（唯一人工急停）；`ch8`=接管开关（高区间启用 `MSP RC OVERRIDE`+`ANGLE`+`ALTHOLD`；**POSHOLD 视阶段 0 光流结论再定**）。其余通道不暴露。
- **失效统一 `LAND`（慢降），不返航**：上位机断链/崩溃 → INAV failsafe 受控降落。**“失联悬停”需失联瞬间 POSHOLD 已激活，可靠性不足，不采用**。
- **地面零点**：**解锁时记录一次**。
- **厘米级返航**：由**另一块 FPGA** 提供外部定位，**不依赖现有硬件**；接口/协议/坐标系**尚未定义**，单独立项。
- **光流质量“先测再定” → 已定论（2026-10-15）**：实测表明**表面纹理主导**——光滑地面 `q≈20–24`（低于 INAV 门限 35，光流不启用）；**鼠标垫/纹理垫 `q≈37–53`（≥35，可启用）**；强光直射反而变差。**结论：飞行/测试区铺纹理地垫（鼠标垫即可）+ 高度 0.3–0.6m + 上电后等数秒**，即可满足 POSHOLD 的“光流健康”前提（仍受无 GPS 解锁拦截限制）。详见 `Host_Lichee_RV_Nano/AGENT_GUIDE.md` §8-11。
- **起飞窗口（2026-10-15 定）**：起飞贴地时 `q<35` 属预期——**起飞/爬升不需要光流**（用测距 → ALTHOLD）。规则：`ANGLE+ALTHOLD 起飞 → 爬到 0.3–0.5m → 等 q≥35 → 再切 POSHOLD`；**上位机按 q 自动门控**（`mon` 已能读 q）。光流无效时 FC 衰减 XY 速度、POSHOLD 不激活（安全）。
- **不加气压计**（无多余接口，方案否决）。
- **定高优先、定点条件重开**：一期只做 **ALTHOLD**（避开 POSHOLD 的 GPS-home 解锁拦截，见下）；POSHOLD 以阶段 0 光流结论为门。
- **看门狗纳入计划**：上位机“控制循环心跳 + 监督线程”，心跳超时 → 停发 RC（借 FC failsafe 落地）；**无人飞行前必做**（接口现在定，实现排在油门接管之后）。

**已核实（2026-10-15，源码）**：POSHOLD/RTH/WP 等会触发 `isUsingNAVModes` → 解锁要求 `navigationPositionEstimateIsHealthy()`（含 `STATE(GPS_FIX_HOME)`），无 GPS 时会**拦截解锁**（`NAV_ARMING_BLOCKER_MISSING_GPS_FIX`）；**ALTHOLD 不在该列表内，不触发**。`eph` 仅在 XY 传感器更新超时时按 100/s 增长（`inav_max_eph_epv` 默认 1000cm），故光流健康是定点稳定的关键。

**剩余只读核实项**：紧急降落是否依赖高度估计。

**✅ 实机只读核实（2026-10-15，FC 经 USB VCP，未改配置）**：`failsafe_procedure = LAND`（确认，配置稿已同步）；`msp_override_channels = 7`；`nav_extra_arming_safety = ALLOW_BYPASS`；`nav_mc_althold_throttle = STICK`；`inav_max_eph_epv = 1000cm`；`inav_w_z_surface_p/v = 3.5/6.1`；`inav_max_surface_altitude = 200cm`（**定高可用高度先限定 0–2m**）；`nav_mc_hover_thr = 1300`。传感器 `RANGEFINDER/OPFLOW=OK`，`BARO/GPS/MAG=NONE`。
**⚠️ 更正**：`inav_reset_altitude` **不会**直接清零 `est.pos.z`（它只重置气压计/GPS 参考）。地面零点在**固件 Z 分支内实现**（offset 在解锁前持续跟随、解锁后冻结），其**时机**仍由该设置决定（`FIRST_ARM`/`EACH_ARM`）。**⚠️ `inav_allow_dead_reckoning = OFF`（POSHOLD 需改 ON）**。

**实现状态（2026-10-15）**：Z 分支已在**新工作树 `Low_MCU/new_mcu/drone/inav-g4alt/`** 实现并编译通过（未刷写）。详见 `Low_MCU/new_mcu/AGENT_GUIDE.md` §5.7。

**实施规划（2026-10-15 定）**

| 阶段 | 内容 | 门限 |
|---|---|---|
| **0 光流/地面测试** | `paradigm mon` 测 `q`/流方向/测距误差（光滑 vs 纹理垫、光照、高度） | q 稳定+方向正确 → 重开 POSHOLD |
| **1 固件 Z 分支** | 测距作 Z 源（`estimationCalculateCorrection_Z`） | 拔桨比对 `est.pos.z`；遮挡验失效 |
| **2 定高启用** | aux=`OVERRIDE+ANGLE+ALTHOLD`；`althold_throttle_type=STICK` | 绑绳悬停 |
| **3 油门接管** | `mask=15` + 上位机发中性（解锁/上锁仍发低位） | 拔串口验 LAND |
| **4 看门狗** | 心跳 + 监督线程 | 无人飞行前 |
| **5 定点（条件）** | POSHOLD + 解锁拦截处理 | 以阶段 0 为门 |
| **6 短飞/集成** | 状态机接真实油门/位置 | 短飞 |

## 4. 阶段状态

| 阶段 | 内容 | 状态 | 下一步 |
|---|---|---|---|
| 0 相机出帧 | 交叉编译 + CSI 抓帧 | 🟢 **`experiments/…/04_vision_module` Form A 出真图**；`05_rtsp_stream` RTSP 预览打通（B7/B9/B11 已解） | 阶段 1（HSV）或 MSP 链路 |
| 1 绿色荧光 | HSV → 视觉→控制闭环（INAV） | 🟢 **台架/板端全通**：检测(06)+链路(07)+打磨(14 EMA/丢检滞回/FC遥测)+可视化(15 RTSP叠加)+撞击(16 IMU)+状态机干跑(17)+**操控下位机(18 MSP RC Override)**+**光流阻尼(19)**+**接管三轴/范式选择(20)** | 台架（压重物/绑绳）验 B追踪/定点/阻尼 → 真机短飞；光流质量一般待改善；定高(O3)需接管油门 |
| **1.5 定高+定点** | 测距→高度（`NAV ALTHOLD`，ch7 中）；光流→位置（`NAV POSHOLD`，ch7 高 [1700,2100)） | **定高 ✅ 达标**（10-17：修复+2m+验证全过，30s 正式测试免测）；**定点 ⏳**：三道门已源码核查完毕（详见下位机指南 §5.7"定点三道门"块） | **定点推进**：改配置（`inav_allow_dead_reckoning=ON` + `aux 5 9 2 1700 2100`）→ 台架验模式 → 真机验保持/降级 → 达标后**才算"下位机调稳"收尾** → 转上位机绿色跟踪 |
| 2 不规则物体 | YOLOv8n NPU 部署 → 撞击 → 30s 返航 | 🟡 **部署(09/10)+量化(11)已通**；自训装甲模型待 4060 | 训练+导出 → 量化 → 部署 |
| 3 自写固件 | 下位机自研飞控 | ⏳ 远期 | 新主控 G473 已重购、INAV G4 能飞；待上位机对接后推进 |

## 5. 当前工程实况

**上位机** `Host_Lichee_RV_Nano/`
- **镜像 = scpcom 构建**（`/boot/ver`≈`6f9596`，内核 `2026-07-14`，中间件 `libsys 87be70b`）；源码树已克隆至 `LicheeSG-Nano-Build_scpcom/`（`scpcom/LicheeSG-Nano-Build`，sophgo weekly 2024.10.14 中间件）。
- 板上 `test_mmf`（scpcom 编）取**相机**帧 → 真图；**自写程序 `experiments/…/03_scpcom_formA`（Form A）也已出真图**（黑帧根因 = 中间件版本混用 + 抓启动首帧，均已在 `04_vision_module` 解决）。注：scpcom 版 `test_mmf 4` 走**相机**（`_test_venc_jpg` 用 `mmf_vi_frame_pop`），sipeed 版的才是合成彩条图。
- 默认 sensor 配置 = **beta**（`lane_id=4,3,2 / mclk=1 / bus4 / i2c 0x29`），由镜像 `/etc/init.d/S02config` 决定：无 `/boot/alpha|epsilon` → beta + pinmux（`devmem 0x0300116C=0x3, 0x0300118C=0x5`）。**我们的 `sensor_cfg.ini` 内容 = beta，并未改坏配置**。
- **实验档案制**（每次尝试一个目录，索引 `Host_Lichee_RV_Nano/experiments/README.md`）：01 低层MMF / 02 sipeed链 / 03 FormA首验 / 04 相机层 / 05 RTSP / 06 绿色检测 / 07 MSP 链路 / 08 识别绿色并飞向(闭环) / 09 NPU 部署 / 10 TDL 推理层 / 11 TPU-MLIR 量化 / 12 传统CV装甲板(PC) / 13 YOLOv8n 量化 / 14 闭环打磨(EMA/滞回/FC遥测) / 15 RTSP叠加 / 16 IMU撞击 / 17 状态机干跑 / **18 操控下位机(MSP RC Override)** / **19 光流水平阻尼** / **20 接管三轴+范式/定点/定高选择**。
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
- **`experiments/2026-10-11_18_msp_takeover/`（🟡上位机操控下位机）**：`takeover` 两模式——`track`（HSV 绿目标→EMA/滞回→roll/pitch→`SET_RAW_RC`，经 FC `MSP RC Override` 接管）+ `probe`（方向自检：发 roll±/pitch±，回读 `MSP_MOTOR`/`ATTITUDE`/`MSP_RC` 定符号/kp/是否真接管）。**纠正 `msp_override_channels` 位序=AERT**：只接管三轴=**7**（FC 已设 7 并 `save`）。**台架验证通过**（接管生效/油门不被覆盖/混控 Δ 与 `mmix` 一致/符号自洽）；**开放问题：水平误差用 roll 还是 yaw（待定）**；真机待标定。
- **`experiments/2026-10-12_19_opflow_hold/`（🟡光流水平阻尼）**：`flow_damp`——新增 **MSP v2** 收发（`msp2_send_request`/`msp_parser2_feed`，吃 v1+v2 帧）+ `mon`（只读光流 `MSP2_INAV_OPTICAL_FLOW` 0x2001 / 测距 `MSP_SONAR_ALTITUDE` 58）+ `damp`（光流估水平速度→roll/pitch 阻尼，依 FC `navigation_pos_estimator_flow.c` 约定）。数据通路已验（`q≈40–60`）。预启动脚本 `run/stop_flowdamp.sh`。
- **`experiments/2026-10-12_20_paradigm_select/`（🟡接管三轴+范式选择）**：`paradigm`——读 `MSP_RC` 取 ch6/ch7；`ovr=(ch6>1300)||(ch8>1300)`；`ch6>1300`→范式B（roll 追踪, `pitch_bias_px` 基准）；`ch7[1300,1700)`→定点（光流阻尼+位置积分）、`[1700,2100)`→定高（**桩**，需 mask=15）；**接管期间始终叠加基础光流阻尼**；`MIROCFLY_CAM_ROT=0|180`（相机倒装冗余）、`alt` 无效值回退。`mon`/`dry` 已验证开关/模式/符号；**待台架真机**。预启动 `run/stop_paradigm.sh`。

**下位机** `Low_MCU/`
- AT32 方案**已冻结**（`archive_at32/agent_at32.md`）。
- 新主控 = **BetaFPV G473（STM32G474）**，与烧毁的同款**已重购**；**自研移植 INAV 10.0.0（STM32G4）当前能飞**；**本土化已落地**（上位机 MSP UART2@230400 链路已打通；CRSF/光流接口已配）。**2026-10-10 解决"离地即翻/自旋"**（根因＝**电机输出编号与物理接线错位**；修复：mixer 轮换版 + `motor_direction_inverted=OFF` + `align_board_roll=1800`）。工作区 `new_mcu/drone/`，详见 `new_mcu/README.md` 与 `new_mcu/AGENT_GUIDE.md`。**开关映射（2026-10-12 定稿）**：`ch5`=ARM；`ch6>1300 或 ch8>1300`=MSP RC OVERRIDE（OR）；`ch8>900`=ANGLE；`ch6`=范式(当前全 B)；`ch7[1300,1700)`=定点/`[1700,2100)`=定高；`msp_override_channels=7`（CLI dump 见 `new_mcu/drone/config/g473_msprc.txt`）。**2026-10-14：飞控改回正装 → 撤销倒装补偿**：`align_board_roll` 1802→**0**、mixer 恢复**标准 QuadX**（REAR_R/FRONT_R/REAR_L/FRONT_L），已 `save`；**电机转向用户另改（ESC/物理）**；**待 §5.5 拔桨复验三轴方向**（回退：align=1800 + 轮换版 mixer）。
- **2026-10-16/17：定高（测距 Z 分支）落地并首飞**：Z 分支固件（`drone/inav-g4alt/`，Oct 15 构建）已刷；`aux 4 3 2 1300 2100`（`ch7≥1300`＝**NAV ALTHOLD**，现"中位即定高"）；**台架**：解锁后 z 跟踪测距（误差 0–3cm，3–83cm）；**首次定高试飞**（黑匣子 `~/Downloads/第一次定高.TXT`，5 段）：`flightModeFlags=10`（ANGLE｜NAV_ALTHOLD）→ **定高真实挂上**、`navFlags bit0=1`（高度估计 TRUSTED）、测距爬升 ~1.66m；**结尾 ~14.4s 翻滚坠机（已归因：见下条）**。用户手感反馈：**水平漂 + 灵敏度高（轻打杆大幅动作）** → 待黑匣子调参 + CoG/校准核对。**SURFACE 模式不可用**（模式盒与 AGL 估计两处硬依赖气压计）。详见 `Low_MCU/new_mcu/AGENT_GUIDE.md` §5.7 / §6-20 / §6-21。
- **2026-10-17 晚：归因闭环 + 参数修复 + 复测 + 失效核实**：① 坠机归因定论 = `nav_mc_hover_thr` 未标定（1300 vs 实测悬停 ~1585-1610）：空中拨入定高瞬间输出基准被拉到 1300 → 下沉 ~1.4m + 低电量 sag + 漂入软网收尾；Z 分支全程无误。② **`nav_mc_hover_thr` 1300→1585**（满电 ANGLE 悬停实测，set+save 已复读）。③ **`inav_max_surface_altitude` 200→400cm**（原 >200cm 测距样本被弃 → reliability 衰减 → Z 源失效＝"2m 软墙"；覆盖 2.5m 需求）。④ 复测（`blackbox_logs/blackbox_log_2026-10-17_161306.TXT` seg4）：**拨入下沉已消除**（rcCommand 1630→1580≈hover_thr；+15cm 过冲、~1.2s 回稳、随后 37.5±3.0cm）；**飞越凳子/箱子触发"地形跟随"式油门摆动**（测距 Z 固有盲区实飞实证）。⑤ **失效链路核实（A5）**：拔上位机串口/接管流超时（~0.7s）→ **静默回落物理遥控、不触发 LAND**；LAND 只由物理 RC 丢失触发（紧急降落状态机，**依赖高度估计**）；"停发 RC 借 failsafe 落地"的看门狗前提**不成立**，phase 4 需重新设计。⑥ **黑匣子字段口径更正**：`navState`=`navPersistentId`（0=UNDEFINED / 1=IDLE / 2=ALTHOLD_INIT / 3=ALTHOLD_IN_PROGRESS）；`mspOverrideFlags` bit1=接管开关激活、bit0=接管流超时。⑦ **唯一小目标**：下位机调稳（**定高 ✅ + 定点 ⏳**）→ 上位机绿色跟踪。定高已达标（2M定高：拨入无下沉、软墙消、爬 3.6m 连续；30s 静止悬停免测关闭）；**定点 = POSHOLD（ch7 [1700,2100)）**：三道门核查完毕（广告门非阻塞 / 解锁用 YAW_HI 手势或手持建 home / 需 `inav_allow_dead_reckoning=ON`+光流健康），待改配置+验证。**下一步：定点推进**。
- **2026-10-17 深夜：光流链路打通（符号修复 + align 标定）**：① **MSP2 光流遥测丢符号 bug**（`fc_msp.c:778-781` float→u16 隐式转换，负值饱和为 0；输入链路 int32、工具端 int16 解析均无误）→ **修复 4 行**（`(int16_t)lrintf(...)`）→ 重编刷 hex（**新版本 `Oct 17 2026 / 19:02:04 (7e2d65a5)`**，FLASH1 458394B；回退件 `firmware/BETAFPVG473-z-branch-e112ce42/`）→ 负值正常出现。② **四组磁带点位实测**（右/左/前/后，聊天书签+快移慢回协议）→ CW0FLIP 下 右→+Y、左→−Y、前→−X、后→+X → 8 矩阵枚举唯一解 → **`align_opflow = CW90`（已 save + 复读）**。③ 改后复测命中预测：**右→+X（+739）、前→−Y（−1088）** ✓。④ `opflow_scale` 维持 **1.133**：手持标定被**死区（慢 <~50°/s 截断，4×1m 慢滑只记 19%）与饱和（快 >~400°/s 虚高，积分 2–2.6× 为伪值）**两头污染 → 改**飞行中标定**（下位机指南 §6-27）。⑤ 下一步：P1（`inav_allow_dead_reckoning=ON` + `aux 5 9 2 1700 2100`）→ 台架 → 真机定点。详见下位机指南 §5.7 / §6-24~27。

## 6. 技术要点速查

- MSP：`MSP_SET_RAW_RC`(200,≥5Hz)、`MSP_RAW_IMU`(102)、`MSP_ATTITUDE`(108)、`MSP_RX_MAP`、`MSP2_INAV_STATUS`、`MSP2_INAV_ESTIMATED_POSITION`；`MSP RC Override` 掩码 `msp_override_channels`。
- 相机：CVI MMF(VI→ISP→VPSS→VENC)；**编译须用 scpcom 公有头 + 链接板上 scpcom 库**（`/mnt/system/usr/lib` + `/3rd`；另需 `-lgdc`）；抓帧先丢前导帧；sensor_cfg GC4653 beta（lane 4,3,2, mclk1；S02config 决定）；ION/carveout 媒体预留 ~128MB（Form A 正常退出可回收，实测 used=0；异常后仍建议干净重启）。
- **相机光学（GC4653，官方镜头）**：焦距 **3.05mm** / 光圈 F2.5 / **畸变 5%**；FOV **对角 90°、水平 81°、竖直 51°**（1/3" 传感器，2560×1440，2.0µm；有效幅面 5.12×2.88mm）。换算：**1m 处约覆盖 1.6m×0.92m，2m 处 ~3.2m×1.85m**（近似，含畸变）。**固定前视时竖直 51° 很宽 → pitch 不敏感**：巡航 1–1.5m 时**下倾 ~10–15°** 可覆盖 ~1.5m~∞；倒装用 `MIROCFLY_CAM_ROT=180`（exp20）、俯仰基准用 `pitch_bias_px` 标定（标法见 `Host…/experiments/2026-10-12_20_paradigm_select/README.md`）。
- RTSP：`experiments/2026-09-26_05_rtsp_stream/`（VI→VENC H265→`rtsp_send_memory_data`）；`rtsp_server_init` **须显式绑 IP**（`RTSP_IP=10.222.2.1`，否则自动绑 wlan0 → USB 连不上，B11）；scpcom 未开放 h264 init；退出 SIGTERM 勿 `kill -9`（否则 ION 泄漏）。
- 绿色检测：opencv-mobile(OpenCV5)。**HSV 标度**：PIL H0-255 vs OpenCV H0-179（`Hcv=Hpil*179/255`）；`findContours/contourArea/moments` 在 **geometry** 模块（`#include <opencv2/geometry.hpp>` + `-lopencv_geometry`）；**无 imgcodecs** → 写 BMP；小目标 `min_area` 要小（B12）。
- NPU：SG2002 代号 cv181x，仅 INT8/BF16；转换链 PyTorch→ONNX→MLIR→INT8 cvimodel；参考 ret7020 YoloCamera 与 MilkV Duo TDL 文档。
- 光流+测距 **MTF-02P**（下视，走 MSP）：**已实测可用**（FC `status` `OPFLOW/RANGEFINDER=OK`；上位机读 `MSP2_INAV_OPTICAL_FLOW`(0x2001)/`MSP_SONAR_ALTITUDE`(58)，`q≈40–60`）。关键规格：**光流最小工作距离 8cm**、环境光 **>60Lux**、光流视场 42°、1m 处最大测速 7m/s、数据 50Hz；**测距 0.02–6m（精度 2cm）**。→ 飞行高度需 **≥8cm**（建议 0.2–0.5m）。

## 7. 给后续 Agent 的要求

- 改动模块前：先读对应指南（`Host_Lichee_RV_Nano/AGENT_GUIDE.md`；下位机历史见 `Low_MCU/archive_at32/agent_at32.md`）。
- 不重复造轮子：优先官方 SDK 示例（TDL/YoloCamera/MilkV Duo）、已有库（opencv-mobile）。
- 上位机结构约定：实验档案制 `experiments/<日期>_<编号>_<描述>/`（自包含、不覆盖）；未来功能各自开新实验（MSP 链路 / 视觉检测 / 控制 / 任务）。任务逻辑与协议编码解耦。
- 测试永远渐进：拔桨 → 绑绳 → 短飞；电机解锁前占空比=0；错误状态必须禁能电机。
- 只做被明确要求的改动，不擅自扩大范围。
- **调试/分析工具**集中在 `tools/`（详见 `tools/README.md`）：`msp_probe`（链路验证）、`msp_live.py`（i5 实时监控）、`telem_mon`（板上只读遥测）、`blackbox_decode.py`（INAV 黑匣子解码/频谱）。
- **维护文档**：推进/方案变更时，同步更新本文件 + `README.md` + 各 `AGENT_GUIDE.md` + `difficulty_and_method.md`。

## 8. 风险与待确认项（按优先级）

1. ~~**自写程序黑帧**（中间件版本不匹配）~~ → ✅ **已解决**：`experiments/…/04_vision_module` Form A 出真图，`05_rtsp_stream` 出 RTSP（B7 版本混用 + B9 首帧黑 + B11 RTSP 绑定均已解）。
2. ION 泄漏：Form A/RTSP 正常退出（SIGTERM）可回收（实测 used=0）；**`kill -9` 会泄漏**，需干净重启；程序清理路径在异常分支仍待加固。
3. **不需要**构建 scpcom 的 kernel/middleware（app 只要公有头 + 板上库）。
4. NPU INT8 对自定义目标精度（HSV 兜底）；**缺 TDL SDK**。
5. 下位机 G473：**上位机 MSP 链路已打通**（复测 `LINK OK`）；**"翻/自旋"已解决（电机编号错位）**；**电压采样已修**（`vbat_scale` 110→1100）。**开关映射已定稿（2026-10-12）并写入 FC**：`ch5`=ARM；`ch6>1300 或 ch8>1300`=**MSP RC OVERRIDE**（同 modeId 两条 aux = OR）；`ch8>900`=ANGLE；`msp_override_channels=7`（只接管 roll/pitch/yaw；位序 **AERT**，`11` 会误接管油门→切油）。**exp18/20 台架+`mon`/`dry` 验证通过**（接管生效、开关读值/模式判定正确、视觉符号自洽）。**光流 MTF-02P 已在线**（`OPFLOW/RANGEFINDER=OK`，`q≈40–60`）**质量一般**。**下一步：台架（压重物/绑绳）验 B追踪/定点/阻尼 → 真机短飞**。**FC 用完 `fccli` 必须 `exit`/`save` + 等 ~15–20s**（否则 UART2 MSP `NO RESPONSE`）。
6. 撞击检测采样率受限 → 与视觉联合判定。
7. **定高/定点（ALTHOLD/POSHOLD）**：本板**无板载气压计**（官方页 `Baro: —`；INAV `BARO=UNAVAILABLE`，`baro_hardware` AUTO 会被写回 NONE）。INAV 的高度前提 `navReadyAltControl` **只认气压计/GPS**，**光流+激光测距不算** → **已确认跳过**。若将来要做：外接 **BMP280/DPS310 到 I2C1**（PA15/PB7），再 `baro_hardware=AUTO` + `inav_allow_dead_reckoning=ON` 即可无 GPS 定高/光流定点。
