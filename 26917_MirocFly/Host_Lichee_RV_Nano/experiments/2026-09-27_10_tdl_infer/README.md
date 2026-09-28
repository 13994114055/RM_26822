# 2026-09-27_10_tdl_infer（实验）

**目的**：把实验 09 的"能跑"升级为**可复用推理层**——封装 TDL 的 **相机取帧 + NPU 检测 + 画框 + RTSP 推流**，对外只暴露干净 C 接口，便于日后提升为 `libmf_tdl`。

**结果**：✅ 打通。相机实时检测（yolov8n coco80）+ 画框 + RTSP 推流，电脑 `ffplay/VLC` 可看。
- RTSP：`rtsp://10.222.2.1:554/h265`（`libcvi_rtsp`，监听 `0.0.0.0:554`），**H265** 1280×720（路径随编码命名：H265→`/h265`，H264→`/h264`）。
- 检测速度：约 **16 FPS**（1280×720）。

## 目录（按"可提升为库"组织）
```
├── include/mf/tdl.h   # 公共 C 接口 (extern "C")
├── src/mf_tdl.cpp     # 实现: 相机/检测/自绘框/RTSP
├── apps/tdl_app.cpp   # 演示主程序
├── thirdparty/        # sample_utils, include_tdl, json.hpp (第三方)
├── libs/              # npu_libs, tdl_libs, libcvi_rtsp (链接输入, gitignore)
├── models/            # yolov8n det coco80 cvimodel (gitignore)
└── Makefile
```

## 公共 C 接口（`mf/tdl.h`）
`mf_tdl_open/close`、`mf_tdl_camera_open/read/release`、`mf_tdl_detect`、`mf_tdl_draw`、`mf_tdl_stream_open/send`。

## 编译 / 运行
```bash
make          # 产物 tdl_app
scp tdl_app models/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel root@10.222.2.1:/root/mirocfly/
LP=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/system/lib:/usr/bin/lib:/maixapp/lib:/mnt/system/opt/cvitek_tpu_sdk/lib
ssh ... "cd /root/mirocfly && (LD_LIBRARY_PATH=$LP ./tdl_app yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel 1280 720 0 >/tmp/app.log 2>&1 &)"
# 电脑观看
ffplay -rtsp_transport tcp rtsp://10.222.2.1:554/h265
```

## 关键坑（重要）
1. **`TDL_WrapImage` 输出的是"帧指针"**：正确写法是
   `VIDEO_FRAME_INFO_S *frame = NULL; TDL_WrapImage(image, &frame);`
   **不是** `VIDEO_FRAME_INFO_S vf; TDL_WrapImage(image, &vf);`。
   传值 → 喂给 VENC 的帧信息错乱 → **RTSP 花屏（绿紫竖条纹）**。改成指针后正常。
2. 相机格式用 **NV12（`IMAGE_YUV420SP_UV`）**，与官方样例一致。
3. **RTSP 在 `libtdl_utils.so`**（`SendFrameRTSP`），只依赖 `libcvi_rtsp.so`（板上已有），**不碰 libcurl**；不要链 `-ltdl_ex`（见 difficulty B13）。
4. 链接：`board_libs` + `-lgcc_s` + `-latomic`；编译第三方头需 `/tdl_sdk/src/c_apis/include`、opencv 头、中间件/osdrv 头。
5. 画框自写（直接写 Y 平面，不依赖 OpenCV/meta_visualize）。

## 硬件插曲（已澄清）
一度以为相机坏了（取帧失败、`vi_detect`/`test_mmf` 也挂），实为**瞬态/资源状态**；干净重启后 `test_mmf`、`vi_detect`、`tdl_app` 均恢复。**相机与上位机串口都是好的**。
