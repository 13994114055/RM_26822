#!/usr/bin/env bash
# 实验13: 一键把 TDL 导出的 YOLOv8 ONNX 量化成 cv181x INT8 cvimodel (TPU-MLIR docker)
# 用法: bash quantize_yolo.sh [onnx相对路径] [模型名] [校准图目录] [输入尺寸]
#   默认: yolov8n/onnx/yolov8n.onnx  yolov8n  calib  640
# 环境: docker 镜像 tpuc_mlir:latest (见实验11 / difficulty B15); 本机 bridge 坏, 量化离线用 --network none
set -euo pipefail

ONNX=${1:-yolov8n/onnx/yolov8n.onnx}
NAME=${2:-yolov8n}
CALIB=${3:-calib}
SIZE=${4:-640}
IMG=${TPUC_IMAGE:-tpuc_mlir:latest}

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

# 测试图(取校准集第一张), 供 model_transform 做 --test_input
TESTIMG="$(ls "$CALIB"/*.jpg 2>/dev/null | head -1 || ls "$CALIB"/*.png 2>/dev/null | head -1)"
[ -n "${TESTIMG:-}" ] || { echo "校准图目录 $CALIB 里没有 jpg/png"; exit 1; }
echo "ONNX=$ONNX  NAME=$NAME  CALIB=$CALIB  SIZE=$SIZE  TESTIMG=$TESTIMG"

DOCKER="docker run --rm --network none -v $HERE:/workspace -w /workspace $IMG"

echo "== 1/3 model_transform (ONNX -> MLIR) =="
$DOCKER model_transform.py \
  --model_name "$NAME" \
  --model_def "$ONNX" \
  --input_shapes "[[1,3,${SIZE},${SIZE}]]" \
  --mean 0.0,0.0,0.0 \
  --scale 0.0039216,0.0039216,0.0039216 \
  --pixel_format rgb \
  --keep_aspect_ratio \
  --test_input "$TESTIMG" \
  --test_result "${NAME}_top_outputs.npz" \
  --mlir "${NAME}.mlir"

echo "== 2/3 run_calibration (需 ~100 张校准图) =="
$DOCKER run_calibration.py "${NAME}.mlir" \
  --dataset "$CALIB" --input_num 100 -o "${NAME}_cali_table"

echo "== 3/3 model_deploy (INT8 -> cv181x cvimodel) =="
$DOCKER model_deploy.py \
  --mlir "${NAME}.mlir" \
  --quantize INT8 \
  --calibration_table "${NAME}_cali_table" \
  --processor cv181x \
  --test_input "${NAME}_in_f32.npz" \
  --test_reference "${NAME}_top_outputs.npz" \
  --tolerance 0.85,0.45 \
  --model "${NAME}_cv181x_int8_sym.cvimodel"

echo "== 产物属主修正(root -> uid) =="
$DOCKER chown -R "$(id -u):$(id -g)" /workspace

echo "== 完成: ${NAME}_cv181x_int8_sym.cvimodel =="
ls -lh "${NAME}_cv181x_int8_sym.cvimodel"
