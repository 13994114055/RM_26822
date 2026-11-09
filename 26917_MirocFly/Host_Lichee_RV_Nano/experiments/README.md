# MirocFly 上位机 — 实验档案索引

> 约定：**每个技术尝试一个独立文件夹**，自包含、不覆盖、不删除，便于回溯与对比。
> 命名：`日期_编号_描述`。每个实验目录内有自身 `README.md`（目的/结论/构建方式）。
> 共享资源放在上层：`../board_libs/`（板上库链接副本，`../fetch_board_libs.sh` 拉取）。

| 编号 | 日期 | 目录 | 目的 | 结果 | 状态 |
|---|---|---|---|---|---|
| 01 | 2026-09-24 | [2026-09-24_01_lowlevel_mmf](./2026-09-24_01_lowlevel_mmf/) | 低层 CVI MMF 直接抓帧 | ❌ scpcom 板上跑不通 | 弃用（含 `nv21_to_bmp.py`/`detect_green.py` 工具） |
| 02 | 2026-09-26 | [2026-09-26_02_sipeed_middleware](./2026-09-26_02_sipeed_middleware/) | 自写 `sophgo_middleware`(NV21) 取帧 | ❌ 全黑（链了 sipeed 中间件） | 弃用（版本混用反例，B7） |
| 03 | 2026-09-26 | [2026-09-26_03_scpcom_formA](./2026-09-26_03_scpcom_formA/) | **隔离实验**：scpcom 源码中间件 + 板上 scpcom 库 | ✅ 出真图（Form A 成立） | 存档（已被 04 取代） |
| 04 | 2026-09-26 | [2026-09-26_04_vision_module](./2026-09-26_04_vision_module/) | Form A 相机层固化（`camera.h` API + `frame_grab`） | ✅ 出真图 | **当前相机层正解** |
| 05 | 2026-09-26 | [2026-09-26_05_rtsp_stream](./2026-09-26_05_rtsp_stream/) | VI 取帧 + VENC(H265) + RTSP 实时预览 | ✅ 主机 ffmpeg 收到 hevc 1280x720（画面真实） | **完成**（绑定 usb0/RTSP_IP=10.222.2.1） |
| 06 | 2026-09-26 | [2026-09-26_06_hsv_green](./2026-09-26_06_hsv_green/) | HSV 绿色检测（阶段1 感知） | ✅ 板端稳定检出 3m/20cm 绿目标 offset/area（存 BMP） | **完成**（opencv-mobile/geometry；未接控制） |
| 07 | 2026-09-26 | [2026-09-26_07_msp_link](./2026-09-26_07_msp_link/) | MSP 链路（SET_RAW_RC 注入 + IMU 回读）+ offset→RC | ✅ 编解码/CRC/PTY 模拟 FC 端到端自测通过 | **完成**（真串口待下位机） |
| 08 | 2026-09-26 | [2026-09-26_08_vision_control](./2026-09-26_08_vision_control/) | **识别绿色并"飞过去"**（检测+MSP 闭环雏形） | ✅ 编译运行通（PTY 假 FC 收 `SET_RAW_RC`；无目标回中） | 闭环已写，真机联调待下位机 |
| 09 | 2026-09-27 | [2026-09-26_09_npu_demo](./2026-09-26_09_npu_demo/) | **NPU 部署入门**（底层 cviruntime + 高层 TDL 检测 + 相机实时） | ✅ cviruntime 226FPS；TDL 图片检测 17.6FPS；**相机实时检测 16.4FPS** | **完成** |
| 10 | 2026-09-28 | [2026-09-27_10_tdl_infer](./2026-09-27_10_tdl_infer/) | **可复用推理层**（相机+TDL检测+画框+RTSP，C 接口） | ✅ RTSP 叠加出真图 `rtsp://10.222.2.1:554/h265`（H265），~16FPS | **完成** |
| 11 | 2026-09-29 | [2026-09-28_11_quantize](./2026-09-28_11_quantize/) | **TPU-MLIR 量化**（ONNX → cv181x INT8 cvimodel） | ✅ resnet18 → cvimodel，板上 NPU 43.9FPS | **完成**（YOLOv8n 待接） |
| 12 | 2026-09-29 | [2026-09-29_12_armor_led](./2026-09-29_12_armor_led/) | **传统 CV 装甲板**（红/蓝 LED 灯条，通道差+几何配对） | ✅ S0+S1：透视合成图 + cv2 原型，合成集召回/精确 100%、中心误差 0.34px | **进行中**（S2 板端移植） |
| 13 | 2026-09-30 | [2026-09-30_13_yolov8_quantize](./2026-09-30_13_yolov8_quantize/) | **YOLOv8n 量化全流程**（TDL 导出 6 分支 ONNX → TPU-MLIR INT8） | ✅ `yolov8n.onnx` → `yolov8n_cv181x_int8_sym.cvimodel`（3.4MB，占位预训练模型） | **完成**（4060 真模型到位只换 ONNX） |
| 14 | 2026-10-10 | [2026-10-10_14_green_fly_smooth](./2026-10-10_14_green_fly_smooth/) | **闭环打磨**（08 闭环 + 偏移 EMA 低通 + 丢检滞回 + FC 遥测回读） | ✅ 台架跑通（真 FC `/dev/ttyS0` @230400）：漏检不回中、offset 平滑、`ATTITUDE/RAW_IMU` 双向回读 | **完成**（`kp`/roll-pitch 符号待实机标定） |
| 15 | 2026-10-10 | [2026-10-10_15_closedloop_overlay](./2026-10-10_15_closedloop_overlay/) | **闭环可视化叠加**（RTSP 上叠加 检测框/offset/RC/FC姿态） | ✅ 台架跑通：RTSP 出图 + 文字/框叠加 + 双向 MSP；检测到绿目标出框+连线 | **完成**（fps≈6 待优化；kp 待实机） |
| 16 | 2026-10-10 | [2026-10-10_16_impact_detect](./2026-10-10_16_impact_detect/) | **撞击检测**（`MSP_RAW_IMU` 加速度模长突增，相对基线） | ✅ 台架跑通：`\|a\|≈1g` 稳定、输入正常；敲击即触发 `*** IMPACT ***` | **完成**（待敲击实测） |
| 17 | 2026-10-10 | [2026-10-10_17_task_state_machine](./2026-10-10_17_task_state_machine/) | **任务状态机雏形**（干跑：IDLE→…→DONE + 视觉/IMU 撞击 + 相机 pitch 偏置参数） | ✅ 台架整链跑通：把绿目标怼近 → `APPROACH→IMPACT→RECOVER→RTH→LAND→DONE` | **完成**（干跑；真机油门/位置待接） |
| 18 | 2026-10-11 | [2026-10-11_18_msp_takeover](./2026-10-11_18_msp_takeover/) | **上位机操控下位机**（HSV 绿目标→roll/pitch→`SET_RAW_RC`，经 FC `MSP RC Override` 接管） | ✅ **台架验证通过**（真 FC `/dev/ttyS0`）：接管生效(`MSP_RC` 回读吻合)、mask=7 油门不被覆盖、混控 Δ 与 `mmix` 一致、符号自洽 | **进行中**（含 INAV 掩码位序 AERT 纠正=7；待定 roll/yaw 与真机） |
| 19 | 2026-10-12 | [2026-10-12_19_opflow_hold](./2026-10-12_19_opflow_hold/) | **上位机光流水平阻尼/定点**（读 `MSP2_INAV_OPTICAL_FLOW` 0x2001 + `MSP_SONAR_ALTITUDE` 58 → 估水平速度 → roll/pitch 阻尼） | 🟡 `mon` 已验光流数据通路（q≈40–60）；`damp` 待台架/真机 | **进行中** |
| 20 | 2026-10-12 | [2026-10-12_20_paradigm_select](./2026-10-12_20_paradigm_select/) | **接管三轴 + 范式/定点/定高选择**（ch6>1300 或 ch8>1300 接管；ch6→范式B；ch7→定点/定高；基础**光流阻尼**始终叠加） | 🟡 **FC aux 已改并 `save`**；`mon`/`dry` 已验证（开关读值/`ovr` 判定/模式切换/视觉符号）；相机倒装 `CAM_ROT`+`alt`修复已加 | **进行中**（待台架真机与调参） |
| 21 | 2026-10-15 | [2026-10-15_21_flow_quality](./2026-10-15_21_flow_quality/) | **阶段 0：光流质量实测**（地面材质/高度/光照对 `q` 的影响；对照 INAV VALID 门限 35） | ✅ **定论**：**表面纹理主导**——光滑面 `q≈20–24`（低于门限，光流不启用）；鼠标垫/纹理垫 `q≈37–53`（≥35，可用）；强光直射反而更差 | **完成**（结论：铺纹理地垫 + 高度 0.3–0.6m + 上电等数秒） |
| 22 | 2026-10-23 | [2026-10-23_22_bench_gate](./2026-10-23_22_bench_gate/) | **权限门验证**（未授权持续发中性 / 授权判据=`ACTIVEBOXES`+`BOXIDS` 对照 / 停发回落）；不含视觉 | 🟡 **真 FC 拔桨 A/B 通过**：ch6 1000↔1503 → PHYS↔AUTH，`BOXIDS[24]=pid50`，mask 位翻转；AUTH 时油门 989 不变（mask=7 实机证据） | **进行中**（C 停发回落、`--wiggle` 轴级跟随、ch8 单独 OR 待验） |
| 23 | 2026-10-25 | [2026-10-25_23_vision_gate](./2026-10-25_23_vision_gate/) | **视觉+权限门合并**（绿目标 HSV → 偏移 → EMA/滞回 → roll/pitch；**仅授权时发非中性**；dry 干跑） | 🟡 交叉编译通过（`vision_gate`，RISC-V）；符号/偏置/增益全 env 可调 | **进行中**（板上 dry 静态标定：四方向符号 + `MIROCFLY_BIAS`） |
| 24 | 2026-11-03 | [2026-11-03_24_vision_control](./2026-11-03_24_vision_control/) | **控制端框架**（L0 VIO 占位 / L1 UDP `vif_frame_t` → L2 选目标/平滑 → L3 映射 → L4 门控+MSP）+ **状态机**（IDLE→ALIGN→APPROACH→COMMIT→IMPACT→RECOVER） | 🟢 **i5 + mock 跑通**：static 全链路、step 映射正确（300px→±150µs）、loss 回退 IDLE、超时链正确 | **进行中**（VIO 解析、mask=15 起飞/降落、真机 AUTH_OFF 验证） |

## 相关文档（上层）
- `../README.md`（项目总览）、`../AGENTS.md`（AI 契约 + 上下文）
- `../AGENT_GUIDE.md`（上位机开发指导）、`../../../困难与解决办法.md`（全项目踩坑档案）
| 26 | 2026-11-08 | [2026-11-08_26_vision_rtsp](./2026-11-08_26_vision_rtsp/) | 检测+画框+RTSP 叠加 + UDP；自绘矩形/点阵数字（无 OpenCV）；`vif_stat` 链路体检 | 🟢 已跑通（复用 exp10 mf_tdl） | 已被 27 取代 |
| 27 | 2026-11-08 | [2026-11-08_27_mirovision](./2026-11-08_27_mirovision/) | **视觉服务 mirovision**：M1 采集→推理→UDP、M2 可选 RTSP 叠加、M3 自恢复/守护/开机自启；`--record` + `vif_play` 录制回放 | 🟢 **在用**：50 张校准后 conf 0.94；倒装 `--rot180`；18–21fps | 生产候选 |
| 28 | 2026-11-08 | [2026-11-08_28_fc_link](./2026-11-08_28_fc_link/) | **上下位机 MSP 链路**：自检工具 `msp_link`（probe/rate/loopback）+ 接线与 INAV 配置手册 | 🟢 **已打通**：ttyS0@230400，4 命令 20/20 | 授权门待台架验证 |
