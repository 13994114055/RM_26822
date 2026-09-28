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

## 相关文档（上层）
- `../README.md`（项目总览）、`../agent.md`（AI 上下文）
- `../AGENT_GUIDE.md`（上位机开发指导）、`../difficulty_and_method.md`（困难与解决，编号 A/B/…）
