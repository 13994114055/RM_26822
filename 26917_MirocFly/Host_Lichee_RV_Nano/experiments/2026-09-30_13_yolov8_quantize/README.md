# 2026-09-30_13_yolov8_quantize（实验）

**目的**：打通 **YOLOv8n 的「导出 → INT8 量化 → cvimodel」全流程**（在 i5，用 TPU-MLIR docker），
为"4060 训练出装甲模型后只换 ONNX 即可量化部署"铺路。

**结果**：✅ 打通。
- `yolov8n.onnx`（TDL **6 分支**：3×box[64] + 3×cls[80]，输入 `[1,3,640,640]`）
- → **`yolov8n_cv181x_int8_sym.cvimodel`（3.4MB，cv181x INT8）**，精度对比通过。
- 意义：证明 **TDL 导出脚本 → TPU-MLIR 三段式** 的 YOLOv8n 通路可用；4060 真模型到位后**只换 ONNX**。

> 说明：本实验用的是**官方预训练 yolov8n.pt**（COCO 80 类）作占位，验证**流程**；精度非目标。

## 环境
- Docker 镜像 `tpuc_mlir:latest`（见实验11 / `difficulty_and_method.md` B15）。
- 容器内**已有 `torch 2.1.0+cpu`**；仅需 `pip install ultralytics`（配 `numpy<2` 以匹配 torch）。
- 本机 bridge 网络坏：装包用 `--network host`，量化离线用 `--network none`。

## 步骤
### 1) 导出 TDL 6 分支 ONNX（容器内）
```bash
# yolov8_export.py 来自 scpcom 树 tdl_sdk/tool/yolo_export/（已拷入本目录）
docker run --rm --network host -v "$PWD":/workspace -w /workspace tpuc_mlir:latest bash -lc '
  pip install -q "numpy<2" ultralytics -i https://pypi.tuna.tsinghua.edu.cn/simple
  python3 yolov8_export.py --weights yolov8n.pt --img-size 640 640'
# -> yolov8n.onnx（本目录；已归档到 yolov8n/onnx/）
```
> `yolov8n.pt` 从 Ultralytics release 下载（本目录已留一份）。

### 2) 三段式量化（`quantize_yolo.sh`）
```bash
bash quantize_yolo.sh            # 默认: yolov8n/onnx/yolov8n.onnx  calib 640
```
内部即 TPU-MLIR 官方流程（TDL《YOLO 开发指南》参数）：
```
model_transform.py --model_name yolov8n --model_def yolov8n/onnx/yolov8n.onnx \
  --input_shapes [[1,3,640,640]] --mean 0.0,0.0,0.0 \
  --scale 0.0039216,0.0039216,0.0039216 --pixel_format rgb --keep_aspect_ratio ...
run_calibration.py yolov8n.mlir --dataset calib --input_num 100 -o yolov8n_cali_table
model_deploy.py --mlir yolov8n.mlir --quantize INT8 --calibration_table yolov8n_cali_table \
  --processor cv181x ... --model yolov8n_cv181x_int8_sym.cvimodel
```

## 目录（入库部分）
```
2026-09-30_13_yolov8_quantize/
├── README.md
├── quantize_yolo.sh             # 三段式量化脚本(可复用)
├── yolov8_export.py             # TDL 官方导出脚本(6 分支)
├── yolov8n.pt                   # 官方预训练权重(占位)
├── yolov8n/onnx/yolov8n.onnx    # ✅ 导出的 ONNX(12MB, 6 分支)
├── yolov8n_cv181x_int8_sym.cvimodel  # ✅ 量化产物(3.4MB)
├── yolov8n_cali_table           # 校准表
└── .gitignore                   # 排除大中间文件(见下)
```
> **不入库**（`.gitignore`）：`*.npz`（如 `yolov8n_top_outputs.npz` 159MB）、`*.mlir`、`work/`、`*_sym/`、`calib/`（100 张，来自实验11 `resnet18/images`）等中间产物。

## 下一步
- **4060 训出装甲模型** → 用 **同一 `yolov8_export.py`** 导出 ONNX（1 类）→ 换掉本目录 ONNX → `quantize_yolo.sh` 量化。
- 板上用 **实验10 的 TDL 推理层**加载新 `cvimodel` 验证（注意 TDL 按模型名/类型识别，自定义 1 类模型需匹配）。
- 校准集换用**装甲相关真帧**（比通用图更贴）。
