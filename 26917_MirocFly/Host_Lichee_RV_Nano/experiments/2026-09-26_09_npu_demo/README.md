# 2026-09-26_09_npu_demo（实验）

**目的**：边缘 AI **部署侧**入门——亲手走"加载模型 → 预处理 → NPU 推理 → 后处理"，并测 FPS。
分步：步骤1 用底层 `cviruntime`（本文件已记录）；步骤2 用高层 TDL（检测/后处理）；步骤3 接相机实时。

## 环境（侦察结论）
- 板上 NPU 运行时齐全：`/dev/cvi-tpu0`、`libcviruntime/libcvikernel/libcvimath`、TDL 运行时 `/mnt/system/lib/libtdl_*.so`。
- 板载模型：`/usr/bin/{mobilenet_v2_rgb_224_int8, resize_net_3_int8, yolov5s_224_int8}.cvimodel`。
- `cviruntime` 头在 `/mnt/system/opt/cvitek_tpu_sdk/include`（本目录 `include/` 为其副本）。
- 官方模型仓库 `sophgo/tdl_models/cv181x/` 有大量 INT8 模型（yolov8n/v5s/… `_det_coco80_640`）。

## 步骤 1：底层 cviruntime（✅ 完成）
`npu_hello.c`：`RegisterModel → GetInputOutputTensors → 填输入 → Forward → 读输出`。

编译：`make`（链接 `npu_libs/` 的 `libcviruntime/libcvikernel/libcvimath`）。
运行（板上）：
```bash
LD_LIBRARY_PATH=/mnt/system/opt/cvitek_tpu_sdk/lib:/usr/bin/lib:/mnt/system/lib \
  ./npu_hello /usr/bin/mobilenet_v2_rgb_224_int8.cvimodel
```
实测结果：
```
target cv181x; IN fmt=INT8 shape=[1,3,224,224] qscale=48.106 zp=0;
OUT fmt=FP32 count=1000 (ImageNet 分类); Forward 4.428 ms/frame, 225.8 FPS
```

**学到**：部署五步、张量元数据(fmt/shape/count/qscale/zp)、量化公式 `real=qscale*(q-zp)`、NCHW、
NPU 速度；**预处理/后处理**是"喂对数据、看懂输出"的关键（下一步 TDL 自动处理）。

## 步骤 2：高层 TDL 检测（✅ 完成）
`tdl_detect.c`：`TDL_CreateHandle → TDL_OpenModel → TDL_ReadImage → TDL_Detection → 读结果`。
TDL 自动做了 **letterbox 预处理 + 解码 + NMS**（这正是对比步骤1"手工"的价值）。

- 模型：官方 `tdl_models/cv181x/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel`（本目录 `models/`，已下载）。
- 交叉链接：**scpcom 的 TDL 头**（`include_tdl/`）+ **板上 TDL 运行时**（`tdl_libs/`）。
- 运行：
```bash
LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/system/lib:/usr/bin/lib:/maixapp/lib:/mnt/system/opt/cvitek_tpu_sdk/lib \
  ./tdl_detect models/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel test.png
```
实测：`objects=1 score=0.903 box=(0,2,620,426)`，**56.97 ms/帧 = 17.6 FPS**（与文档 17~27 FPS 吻合）；
框准确套住图中的"人"（见 `tdl_box.png`）。`name` 为空是因未加载标签配置（不影响框）。

### 步骤2 的链接三坑（重要）
1. **TDL 需要板上中间件符号**（`CVI_SYS_/VPSS_/VENC_/VI_…`），musl 不会自动加载 → 必须把 `board_libs` 显式链进本程序（同 `-lgdc` 教训）。
2. **`hidden symbol __sync_fetch_and_add_1 ... referenced by DSO`**（A3 坑）→ 链接加 **`-lgcc_s`**（用共享 libgcc，而非静态 libgcc.a 的隐藏符号）。
3. **`libtdl_ex.so` 依赖 `libcurl`**，板上 libcurl/libssl 版本不匹配（`SSL_get0_group_name` 缺失）→ 基础检测不需要它，**不链 `-ltdl_ex`** 即可绕开。

## 步骤 3：接相机实时检测（✅ 完成）
`vi_detect.c`：用 TDL 内部 VI 取帧（`sample_utils.cpp` 的 `InitCamera/GetCameraFrame`）+ `TDL_Detection`，
**TDL 自己初始化相机并实时检测**。
- 运行：`./vi_detect models/yolov8n_det_coco80_640_640_INT8_cv181x.cvimodel 1280 720 60`
- 实测：相机 1280×720，**61.03 ms/帧 ≈ 16.4 FPS**，60 帧累计检出 22 个目标。
- 编译要点：C+C++ 混编；`sample_utils.cpp` 依赖 TDL 内部头 + **nlohmann/json**（`thirdparty/json.hpp`）+ OpenCV 头（用本地 opencv-mobile 头顶，运行时用板载 4.5）；本地副本去掉了 RTSP 依赖。

至此阶段 2 部署链路完整：**相机 → TDL 预处理/推理/后处理 → 实时检测**。


## 目录
```
├── npu_hello.c        # 步骤1
├── include/           # cviruntime 头(板载副本)
├── npu_libs/          # 链接用 NPU 库(板载副本, gitignore)
├── Makefile
└── README.md
```
