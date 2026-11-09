#!/bin/sh
# mirovision 守护脚本：崩溃自动重启，日志按大小滚动
# 用法: ./run_mirovision.sh [后台]
#   ./run_mirovision.sh        前台运行（Ctrl-C 退出）
#   ./run_mirovision.sh bg     后台运行，PID 写入 /tmp/mirovision.pid
set -u

DIR=/root/models
LOG=/tmp/mirovision.log
PIDFILE=/tmp/mirovision.pid
MAXKB=4096

export LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/system/lib:/usr/bin/lib:/maixapp/lib:/mnt/system/opt/cvitek_tpu_sdk/lib
cd "$DIR" || exit 1

CMD="./mirovision --model best_v2_cv181x_int8_sym.cvimodel --classes classes.txt --rot180 --rtsp --stats 1"

supervise() {
	while true; do
		# 日志滚动：超过 4MB 就清空
		if [ -f "$LOG" ] && [ "$(du -k "$LOG" 2>/dev/null | cut -f1)" -gt "$MAXKB" ] 2>/dev/null; then
			mv -f "$LOG" "$LOG.old"
		fi
		echo "===== $(date) start =====" >> "$LOG"
		$CMD >> "$LOG" 2>&1
		echo "===== $(date) exit=$? 2s 后重启 =====" >> "$LOG"
		sleep 2
	done
}

if [ "${1:-}" = "bg" ]; then
	( supervise & echo $! > "$PIDFILE" )
	echo "mirovision 已在后台启动，PID=$(cat $PIDFILE)，日志 $LOG"
else
	supervise
fi
