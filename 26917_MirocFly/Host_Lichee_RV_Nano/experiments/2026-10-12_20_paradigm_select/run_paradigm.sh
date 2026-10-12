#!/bin/sh
# 预启动 paradigm(run) —— 后台运行 + 写日志 + 限时自动干净退出（脱离电脑测试）
# 用法(板上): ./run_paradigm.sh [secs=120] [kpvis=0.4] [kd=150] [kpp=60]
SECS=${1:-120}
KPVIS=${2:-0.4}
KD=${3:-150}
KPP=${4:-60}
cd /root/mirocfly || exit 1
killall -q paradigm 2>/dev/null
sleep 1
LOG="/root/mirocfly/paradigm_$(date +%Y%m%d_%H%M%S).log"
export LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
MIROCFLY_SECS="$SECS" MIROCFLY_KP="$KPVIS" MIROCFLY_KD="$KD" MIROCFLY_KPP="$KPP" \
  nohup ./paradigm run 1280 720 0 /dev/ttyS0 230400 >"$LOG" 2>&1 &
echo "$LOG" > /root/mirocfly/last_paradigm.txt
sleep 1
echo "started: secs=$SECS kpvis=$KPVIS kd=$KD kpp=$KPP"
echo "log=$LOG"
ps 2>/dev/null | grep -E "paradigm" | grep -v grep
