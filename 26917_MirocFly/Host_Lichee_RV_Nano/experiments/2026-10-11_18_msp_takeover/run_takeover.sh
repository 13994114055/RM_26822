#!/bin/sh
# 预启动 takeover —— 后台运行 + 写日志 + 限时自动干净退出（脱离电脑测试用）
# 用法(板上): ./run_takeover.sh [secs=60] [kp=0.5] [pitch_bias=0] [mode=track]
#   secs=0 -> 不限时(需手动 ./stop_takeover.sh 或重新插 USB 后 killall)
# 说明: nohup 脱会话, 拔掉 USB 后进程继续跑; 到 secs 后程序自行 close 相机会干净退出(SIGTERM 语义)
SECS=${1:-60}
KP=${2:-0.5}
BIAS=${3:-0}
MODE=${4:-track}
cd /root/mirocfly || exit 1
killall -q takeover 2>/dev/null
sleep 1
LOG="/root/mirocfly/takeover_$(date +%Y%m%d_%H%M%S).log"
export LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
MIROCFLY_SECS="$SECS" MIROCFLY_KP="$KP" \
  nohup ./takeover "$MODE" 1280 720 0 /dev/ttyS0 230400 "$BIAS" >"$LOG" 2>&1 &
echo "$LOG" > /root/mirocfly/last_log.txt
sleep 1
echo "started: mode=$MODE secs=$SECS kp=$KP bias=$BIAS"
echo "log=$LOG"
ps 2>/dev/null | grep -E "takeover" | grep -v grep
