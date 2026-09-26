#!/bin/bash
# 部署并运行抓帧程序 (VI 直取, 可调 VI 通道输出尺寸) 到 LicheeRV Nano
# 用法: ./deploy.sh [chn_w] [chn_h] [frames] [vb_blocks]
set -e

BOARD="root@10.222.2.1"
DEST="/root/mirocfly"
W=${1:-1280}   # VI 通道输出宽
H=${2:-720}    # VI 通道输出高
N=${3:-1}      # frames
B=${4:-2}      # vb_blocks

echo "== 创建目录 + 清旧文件 =="
ssh "$BOARD" "mkdir -p $DEST/lib /mnt/data && rm -f $DEST/capture $DEST/capture_*.raw" 2>/dev/null || true

echo "== 拷贝二进制 + 传感器配置 + 运行时库 =="
scp capture sensor_cfg.ini "$BOARD:$DEST/"
scp libs/libini.so libs/libstdc++.so.6 libs/libgcc_s.so.1 libs/libatomic.so.1 "$BOARD:$DEST/lib/"
ssh "$BOARD" "cp $DEST/sensor_cfg.ini /mnt/data/sensor_cfg.ini"

echo "== 板子上运行 (chn=${W}x${H} frames=$N vb_blocks=$B) =="
ssh "$BOARD" "cd $DEST && LD_LIBRARY_PATH=/mnt/system/usr/lib:$DEST/lib ./capture $W $H $N $B"

echo "== 回传 raw + 转 BMP =="
scp "$BOARD:$DEST/capture_${W}x${H}.raw" ./capture_${W}x${H}.raw
python3 nv21_to_bmp.py capture_${W}x${H}.raw $W $H capture_${W}x${H}.bmp
echo "完成: 看本机 capture_${W}x${H}.bmp"