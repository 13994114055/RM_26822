# 2026-10-10_15_closedloop_overlay（实验）

**目的**：把"视觉→运动闭环"**可视化**——在 **RTSP 实时画面**上叠加闭环信息，一眼看清：
- 绿色**检测框** + 目标**平滑点**（十字）+ 中心到目标的**连线**；
- 左上角三行文字：`状态/off`（TRACK/HOLD/LOST + 平滑后 offset）、`RC roll/pitch/thr`、`FC r/p/y acc`（FC 回读姿态/加速度）。

> 这样"感知→决策→下发→下位机状态"整条链路**看得见**，便于台架验证与面试演示。

## 数据流（改编自实验05 `rtsp_grab` + 实验14 闭环）
```
VI pop(NV21) ──> [om_process(): NV21->BGR -> HSV 绿检 -> 偏移EMA平滑+丢检滞回
                   -> 在本帧 Y 平面画框/文字(putText)]
             ──> offset->RC -> MSP SET_RAW_RC + 读 ATTITUDE/RAW_IMU   (双向)
             └──> VENC(H265) -> RTSP :8554/live
```

## 结构（C + C++ 分工，避免 maix_mmf.h 的 C/C++ 链接混用）
```
├── loop_overlay.c   # C: MMF/VI/VENC/RTSP 管线 + MSP 闭环 + 主循环
├── overlay_cv.cpp   # C++: opencv 检测/平滑/画框/文字 (om_process)
├── overlay_api.h    # C<->C++ 接口 (om_ctx_t / om_state_t)
├── msp.c/h          # MSP 链路(复制自 07/14)
├── Makefile         # 目标 loop_overlay (opencv + media_server + board_libs)
└── README.md
```
> 关键：`maix_mmf.h`（`.hpp`）的 API 声明**不在 extern "C" 块内**，C++ 里直接 include 会链接不到 C 实现 → 故拆成 **C 管线 + C++ 检测**两个编译单元。

## 用法
```
./loop_overlay [w] [h] [type] [fps] [bind_ip] [serial_dev] [baud]
# 例:
LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd \
  ./loop_overlay 1280 720 1 30 10.222.2.1 /dev/ttyS0 230400
# serial_dev=none -> 只推流(不做 MSP)
```
观看：`ffplay -rtsp_transport tcp rtsp://10.222.2.1:8554/live`

## 台架实测（2026-10-10，真 FC `/dev/ttyS0@230400`）
- RTSP 出图 + 三行文字叠加 + 中心十字 ✓；
- 检测到绿目标时：日志 `HOLD off=(-323,-80) RC=(1339,1540)`，画面上出现**绿框 + 连线** ✓；
- FC 遥测回读：`FC r=-179.4 p=-4.2 y=34.6 acc=38,-5,-512` ✓（roll≈-180° 是因飞机在床上倒/侧放，与链路无关）。

## 注意 / 已知
- **fps≈6**：每帧 `select(6ms)` 等 MSP 回包 + opencv 检测，偏慢；后续可异步化/降分辨率优化。
- **RTSP 单连接**：ffplay 占用时 `ffmpeg` 抓帧会失败（先 `pkill ffplay` 再抓）。
- 相机/推流程序**同一时刻只能跑一个**；退出用 **SIGTERM**（`killall loop_overlay`），别 `kill -9`（ION 泄漏）。
- 安全：油门固定 **1000（不转）**；`kp/符号` 仍需**实机标定**。

## 后续
- fps 优化（MSP 异步线程 / 降 DET 分辨率 / 跳帧）；
- 接入 YOLO（`experiments/10`）替换 HSV；
- 任务状态机（起飞→搜索→接近）。
