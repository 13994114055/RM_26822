# 2026-09-28_11_quantize（实验）

**目的**：上手 **TPU-MLIR 量化**（边缘 AI 最关键、面试最值钱的一段）——把一个 ONNX 模型编译成 **cv181x INT8 `cvimodel`**，并在板上 NPU 验证。先拿**分类模型**练手。

**结果**：✅ 打通。`resnet18.onnx`（随机权重）→ `resnet18_cv181x_int8_sym.cvimodel`（11.87MB）→ 板上 `npu_hello` 推理 **22.77 ms/帧 ≈ 43.9 FPS**。

## 环境（重要：绕开 Docker Hub）
- 国内直连 Docker Hub 极慢、镜像站又坏 → 改从**算能国内 CDN**下**打包镜像**：
  `https://sophon-assets.sophon.cn/sophon-prod-s3/drive/25/04/15/16/tpuc_dev_v3.4.tar.gz`（2.12GB）
  `curl -L -C - -o tpuc_dev_v3.4.tar.gz <URL>`  →  `docker load -i tpuc_dev_v3.4.tar.gz`
- 镜像里**没有 tpu_mlir**，需在容器内 `pip install tpu_mlir`（清华源）；装完 `docker commit` 成 `tpuc_mlir:latest` 复用。

## 步骤
```bash
# 1) 载入镜像(已从 CDN 下载)
docker load -i tpuc_dev_v3.4.tar.gz
# 2) 起容器(本机 docker bridge 损坏 -> 用 --network none; 量化离线不需要网)
docker run --rm --network none -v <本目录>:/workspace -w /workspace tpuc_mlir:latest bash
# 2b) 一次性: 容器内 pip install tpu_mlir -i https://pypi.tuna.tsinghua.edu.cn/simple ; 然后 docker commit

# 3) 准备 ONNX + 校准图 (本实验用 torchvision resnet18 + 我们相机帧 115 张)
# 4) 三段式:
model_transform.py --model_name resnet18 --model_def onnx/resnet18.onnx \
  --input_shapes [[1,3,224,224]] --mean 123.675,116.28,103.53 \
  --scale 0.01712475,0.017507,0.01742919 --pixel_format rgb \
  --test_input images/img_001.jpg --test_result resnet18_top_outputs.npz --mlir resnet18.mlir
run_calibration.py resnet18.mlir --dataset images --input_num 100 -o resnet18_cali_table
model_deploy.py --mlir resnet18.mlir --quantize INT8 --calibration_table resnet18_cali_table \
  --processor cv181x --test_input resnet18_in_f32.npz --test_reference resnet18_top_outputs.npz \
  --tolerance 0.85,0.45 --model resnet18_cv181x_int8_sym.cvimodel
```
板上验证：`./npu_hello resnet18_cv181x_int8_sym.cvimodel`（见实验09）。

## 关键坑（详见 difficulty B15）
1. **Docker 内容存储被"中断的拉取"搞脏** → `docker run` 报 `content digest ... not found`；解法：`docker rmi` + `docker image prune -f` + 重新 `docker load`。
2. **本机 docker bridge 网络损坏**（`failed to add ... pair interfaces: operation not supported`）→ 所有 `docker run` 加 `--network none`（量化不需要网络）。
3. **tpu_mlir 不在镜像里** → 容器内 pip 装，再 `docker commit`。
4. **容器生成的文件是 root 属主** → `docker run ... chown -R 1000:1000 /workspace`。

## 产出
- `resnet18/resnet18_cv181x_int8_sym.cvimodel`（11.87MB，cv181x INT8）
- 流程可复用到 **YOLOv8n**：只需把 ONNX 换成 **TDL `yolov8_export.py` 导出的版本**（去掉解码、6 分支），`--processor cv181x` 不变。

## 下一步
- 用 TDL 导出脚本量化 **YOLOv8n** → 部署（实验10 推理层）→ 装甲板检测。
