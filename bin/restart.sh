#!/bin/bash
#
# 服务器重启脚本
# 先调用 stop.sh 停止所有进程，再调用 start.sh 启动
# 使用方式: ./restart.sh
#

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
STOP_SCRIPT="${SCRIPT_DIR}/stop.sh"
START_SCRIPT="${SCRIPT_DIR}/start.sh"

echo "========================================="
echo " GameServer 重启脚本"
echo "========================================="

# 1. 停止服务器
echo "-----------------------------------------"
echo " [1/2] 停止服务器"
echo "-----------------------------------------"
if [ ! -x "${STOP_SCRIPT}" ]; then
    echo "Error: 停止脚本不存在或不可执行: ${STOP_SCRIPT}"
    exit 1
fi
"${STOP_SCRIPT}"
if [ $? -ne 0 ]; then
    echo "[WARN] 停止过程出现错误，继续尝试启动"
fi

# 2. 等待端口释放（避免端口仍在 TIME_WAIT 状态导致 bind 失败）
echo "-----------------------------------------"
echo " 等待端口释放..."
echo "-----------------------------------------"
sleep 2

# 3. 启动服务器
echo "-----------------------------------------"
echo " [2/2] 启动服务器"
echo "-----------------------------------------"
if [ ! -x "${START_SCRIPT}" ]; then
    echo "Error: 启动脚本不存在或不可执行: ${START_SCRIPT}"
    exit 1
fi
"${START_SCRIPT}"

echo "========================================="
echo " 重启完成"
echo "========================================="
