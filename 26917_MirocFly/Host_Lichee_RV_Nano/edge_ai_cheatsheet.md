# 边缘 AI 复习卡（MirocFly / SG2002）

> 面试复习用。围绕本项目真实工具链（TDL SDK + TPU-MLIR + cvimodel + tdl_models）整理。
> 关联：`AGENT_GUIDE.md`、`experiments/2026-09-26_09_npu_demo/`、`difficulty_and_method.md` B13。

---

## 0. 一句话总览

**端侧 AI = 在 PC 训练、在边缘芯片 NPU 推理**；中间靠"编译+量化"把模型变成 NPU 认得的格式。

```
数据 → 训练(PC/GPU) → 导出 ONNX → 编译+量化(TPU-MLIR) → 部署(.cvimodel) → 推理(NPU) → 业务
 ①        ②              ③               ④                   ⑤           ⑥        ⑦
```
前 3 步离线（电脑），后 3 步在线（板子）。

---

## 1. SDK 是什么

**SDK = Software Development Kit（软件开发工具包）**：厂商给的一套"开发包"（头文件+库+样例+文档+工具）。

| 名字 | 含义 | 作用 |
|---|---|---|
| LicheeRV-Nano-Build | Sipeed 官方构建 SDK | 编整个 Linux 镜像 |
| LicheeSG-Nano-Build_scpcom | scpcom 版构建 SDK | 相机/RTSP 代码来源 |
| **TDL SDK** | **Turnkey Deep Learning SDK**（算能/Sophgo） | 板上 **NPU 高层框架**（加载/预处理/后处理） |
| TPU-MLIR | MLIR-based TPU 编译器 | ONNX → cvimodel（编译+INT8 量化） |
| opencv-mobile | 轻量 OpenCV SDK | HSV 检测等图像处理 |

> SDK 是"框架/工具包"，不是模型；**TDL 承载 YOLO**。

---

## 2. 关键概念

- **张量 (Tensor)**：带类型的多维数组（数据容器）。`[1,3,224,224]`=NCHW 图像；`[1,1000]`=1000 类分数。
- **前向 (Forward/推理)**：输入张量逐层算到输出张量；`CVI_NN_Forward` 即执行一次。训练才用**反向传播**。
- **量化**：FP32→INT8，NPU 只高效跑 INT8。**公式 `real = qscale × (q − zero_point)`**。
  - PTQ（训练后量化，需校准集 100~1000 张）/ QAT（量化感知训练，更准更难）。
- **cvimodel**：cv181x 平台的编译+量化产物；BM 系列用 bmodel。
- **预处理**：letterbox（保持长宽比补边）、归一化、RGB/BGR 顺序（错则精度崩）。
- **后处理**：解码 + **NMS**（去重叠框）+ 置信度阈值。

---

## 3. 部署推理五步（必背）

```
1) 加载模型      CVI_NN_RegisterModel / TDL_OpenModel
2) 取张量        CVI_NN_GetInputOutputTensors
3) 填输入(预处理) SetTensorPtr/Feed + letterbox/归一化
4) 前向          CVI_NN_Forward
5) 读输出(后处理) 解码 + NMS / argmax
```
**底层 cviruntime**：pre/post 自己写；**高层 TDL**：框架自动做。

---

## 4. YOLO 家族

- **YOLO = You Only Look Once**，单阶段目标检测。
- 迭代（多分支，非线性）：v1(2015)→v3(2018)→v4(2020)→**v5(2020 Ultralytics, 工业常用)**→v6(美团)→v7→**v8(2023: anchor-free、多任务)**→v9→**v10(2024: NMS-free)**→v11/v12。
- 规模档：**n/s/m/l/x**（nano 最小最快，边缘用）。**YOLOv8n = nano**。
- 任务方向：检测 / 分割 / 姿态关键点 / 分类 / 跟踪。
- 检测术语：anchor-based(v5) vs **anchor-free(v8)**；NMS；mAP@0.5；输入尺寸 320/416/640。

---

## 5. TDL 官方模型库（`sophgo/tdl_models`）

命名：`<架构>_<任务>_<类别>_<宽x高>_<精度>_<平台>`，例 `yolov8n_det_coco80_640_640_INT8_cv181x`。

| 方向 | 代表 |
|---|---|
| 通用检测 | YOLOv5/6/7/8/10/11、PP-YOLOE、YOLOX（COCO80 或场景专属） |
| 自定义检测 | YOLOV5/6/7/8/10/PPYOLOE/YOLOX（指定 `num_cls`）——**自训入口** |
| 人脸检测/属性/关键点 | SCRFD、RetinaFace、性别年龄眼镜/口罩/情绪、5 点 |
| 分类 | 口罩/活体/手势、ISP 场景 |
| 关键点/姿态 | 车牌4点、手21点、人体17点 |
| 车道线 / OCR | LSTR / 车牌识别 |
| 分割 | YOLOv8-seg、TopFormer |
| 特征/多模态 | 图像特征、CLIP、人脸特征 |
| 跟踪 / 语音 | FearTrack / Zipformer |

**注意**：官方库**没有装甲板模型** → 要么自训（自定义检测），要么传统 CV。

---

## 6. 本项目实测数据（SG2002，1 TOPS INT8）

| 模型 | 方式 | 结果 |
|---|---|---|
| MobileNetV2@224 | 底层 cviruntime | **4.43 ms/帧 ≈ 226 FPS** |
| YOLOv8n@640 INT8 | 高层 TDL | **56.97 ms/帧 ≈ 17.6 FPS**（文档 17~27） |

---

## 7. 工程坑（体现动手能力）

- **交叉编译要"头文件 + 库"对齐板子版本**（版本混用 → 取帧全黑 B7）。
- **DSO 符号可见性**：板载 `.so` 未声明依赖时，musl 不会自动加载其符号 → 显式链接（如 `-lgdc`、TDL 要链 `board_libs`）。
- **隐藏原子符号**：`hidden symbol __sync_fetch_and_add_1 in libgcc.a referenced by DSO` → `-lgcc_s`。
- **传递依赖版本错配**：`libtdl_ex` 拉 `libcurl`，板上 libcurl/libssl 不匹配 → 不链它。
- **INT8 转换是门槛**：TPU-MLIR docker + 校准集 + 导出脚本（去掉检测头解码，量化才友好）。
- **ION 内存**：相机/推理反复跑会泄漏 → SIGTERM 收尾、必要时 reboot。

---

## 8. 面试速记（背这 6 条）

1. 端侧 AI 流程：训练→ONNX→INT8 量化(cvimodel)→板上 NPU 推理；pre/post 由 TDL 负责。
2. SDK vs 模型：SDK 是框架（TDL），模型是网络（YOLO）；TDL 承载 YOLO。
3. YOLO：单阶段；v8 anchor-free/多任务；n/s/m/l/x；v8n 适合边缘。
4. 量化公式 `real = qscale×(q−zp)`，INT8 是边缘命根。
5. 实测：YOLOv8n@640 INT8 约 17.6 FPS。
6. 动手能力：交叉编译头/库对齐、DSO 符号、INT8 转换与校准集。
