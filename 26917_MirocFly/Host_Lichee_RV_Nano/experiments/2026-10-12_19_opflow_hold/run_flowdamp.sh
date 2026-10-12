#!/bin/sh
# 预启动 flow_damp damp —— 后台运行 + 写日志 + 限时自动干净退出（脱离电脑测试）
# 用法(板上): ./run_flowdamp.sh [secs=90] [kd=150] [sx=1] [sy=1]
#   secs=0 -> 不限时(需 ./stop_flowdamp.sh 或插回 USB 后 killall)
SECS=${1:-90}
KD=${2:-150}
SX=${3:-1}
SY=${4:-1}
cd /root/mirocfly || exit 1
killall -q flow_damp 2>/dev/null
sleep 1
LOG="/root/mirocfly/flowdamp_$(date +%Y%m%d_%H%M%S).log"
export LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
MIROCFLY_SECS="$SECS" MIROCFLY_KD="$KD" MIROCFLY_SX="$SX" MIROCFLY_SY="$SY" \
  nohup ./flow_damp damp /dev/ttyS0 230400 >"$LOG" 2>&1 &
echo "$LOG" > /root/mirocfly/last_flowdamp.txt
sleep 1
echo "started: secs=$SECS kd=$KD sx=$SX sy=$SY"
echo "log=$LOG"
ps 2>/dev/null | grep -E "flow_damp" | grep -v grep
