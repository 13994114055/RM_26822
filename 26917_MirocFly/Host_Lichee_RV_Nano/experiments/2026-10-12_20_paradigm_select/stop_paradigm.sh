#!/bin/sh
# 停止 paradigm（SIGTERM，别 kill -9）
if killall -q paradigm 2>/dev/null; then
	echo "stopped (SIGTERM)"
else
	echo "no paradigm running"
fi
