#!/bin/sh
# 停止 flow_damp（SIGTERM，别 kill -9）
if killall -q flow_damp 2>/dev/null; then
	echo "stopped (SIGTERM)"
else
	echo "no flow_damp running"
fi
