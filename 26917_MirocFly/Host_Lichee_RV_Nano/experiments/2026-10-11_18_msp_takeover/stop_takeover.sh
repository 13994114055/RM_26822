#!/bin/sh
# 停止 takeover（SIGTERM，干净关相机防 ION 泄漏；别用 kill -9）
if killall -q takeover 2>/dev/null; then
	echo "stopped (SIGTERM)"
else
	echo "no takeover running"
fi
