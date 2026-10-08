#!/usr/bin/env bash
# 实验12 S2a: 一条命令采集真实帧 -> scp 回 PC -> NV21 转 PNG。
# 用法: bash capture_real.sh <tag> [frames=1] [w=1280] [h=720]
#   例: bash capture_real.sh red_front 1
# 说明: 板上用 frame_grab(实验04) 抓 NV21 raw; 主机端用 nv21_to_bmp.py 转 BMP 再存 PNG。
set -euo pipefail

TAG=${1:?用法: capture_real.sh <tag> [frames] [w] [h]}
FRAMES=${2:-1}
W=${3:-1280}
H=${4:-720}

HERE="$(cd "$(dirname "$0")" && pwd)"
EXP="$(cd "$HERE/.." && pwd)"
OUT_RAW="$EXP/real/raw"
OUT_PNG="$EXP/real/png"
NV="$EXP/../2026-09-24_01_lowlevel_mmf/nv21_to_bmp.py"
BOARD=root@10.222.2.1
SSHOPT=(-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no)
LP=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd

mkdir -p "$OUT_RAW" "$OUT_PNG"

echo "[1/3] 板上抓 $FRAMES 帧 (${W}x${H}) ..."
ssh "${SSHOPT[@]}" "$BOARD" \
  "cd /root/mirocfly && rm -f cam_*.raw && \
   (LD_LIBRARY_PATH=$LP ./frame_grab $W $H $FRAMES 10 0 >/tmp/fg.log 2>&1 &); \
   sleep 10; killall frame_grab 2>/dev/null; \
   tail -2 /tmp/fg.log; ls cam_*.raw"

echo "[2/3] scp 回 PC ..."
scp -q "${SSHOPT[@]}" "$BOARD:/root/mirocfly/cam_*.raw" "$OUT_RAW/"

echo "[3/3] 转 PNG ..."
shopt -s nullglob
for f in "$OUT_RAW"/cam_*.raw; do
  base=$(basename "$f" .raw)          # cam_1280x720 或 cam_1280x720_0
  rest=${base#cam_}                   # 去掉 cam_ 前缀: 1280x720 或 1280x720_0
  if [[ "$rest" == *_* ]]; then suffix="_${rest##*_}"; else suffix="_0"; fi
  bmp="$OUT_RAW/${TAG}${suffix}.bmp"
  python3 "$NV" "$f" "$W" "$H" "$bmp" >/dev/null
  python3 -c "from PIL import Image; Image.open('$bmp').save('$OUT_PNG/${TAG}${suffix}.png')"
  rm -f "$bmp" "$f"
  echo "  -> real/png/${TAG}${suffix}.png"
done
echo "完成。"
