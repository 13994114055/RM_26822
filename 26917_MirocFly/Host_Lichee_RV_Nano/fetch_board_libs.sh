#!/usr/bin/env bash
# 拉取板上 scpcom 运行时库, 供交叉编译链接 (Form A)。
# 这些 .so 只作链接输入, 不部署到板子; 运行时用板上 /mnt/system/usr/lib。
set -eu
BOARD="${BOARD:-root@10.222.2.1}"
DEST="$(cd "$(dirname "$0")" && pwd)/board_libs"
SSH_OPTS=(-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no)

mkdir -p "$DEST"
scp "${SSH_OPTS[@]}" "$BOARD:/mnt/system/usr/lib/*.so" "$DEST/"
scp "${SSH_OPTS[@]}" "$BOARD:/mnt/system/usr/lib/3rd/*.so" "$DEST/"
echo "fetched $(ls "$DEST"/*.so 2>/dev/null | wc -l) libs into $DEST"
