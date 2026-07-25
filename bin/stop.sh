#!/bin/bash
#
# 服务器停止脚本
# 按依赖逆序停止所有由 start.sh 启动的进程
# allinone 和 robot 不包含在内
#
# 停止顺序：space → game → login → dbmgr → appmgr
# 先发 SIGTERM 优雅退出，超时后 SIGKILL 强杀
#

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BIN_DIR="${SCRIPT_DIR}"

# 优雅退出等待秒数，超时后强杀
TERM_WAIT=5

# 判断 Debug/Release 模式（与 start.sh 一致）
MODE=""
if [ -f "${BIN_DIR}/appmgrd" ]; then
    MODE="debug"
elif [ -f "${BIN_DIR}/appmgr" ]; then
    MODE="release"
else
    echo "Error: 未找到任何可执行文件"
    exit 1
fi

# 获取可执行文件名（debug 加 'd' 后缀，release 不加）
get_exe_name() {
    local proc_name=$1
    if [ "${MODE}" = "debug" ]; then
        echo "${proc_name}d"
    else
        echo "${proc_name}"
    fi
}

# 停止单个进程：先 SIGTERM 优雅退出，超时后 SIGKILL
stop_proc() {
    local proc_name=$1
    local exe_name=$(get_exe_name "$proc_name")

    local pids=$(pgrep -x "$exe_name" 2>/dev/null)
    if [ -z "$pids" ]; then
        echo "[SKIP] ${exe_name} - 未运行"
        return 0
    fi

    echo "[STOP] ${exe_name} (PID: $(echo ${pids} | tr '\n' ' '))"

    # 先发 SIGTERM 优雅退出（进程有信号处理，会设置 IsStop 并清理线程）
    kill -TERM ${pids} 2>/dev/null

    # 等待进程退出
    local waited=0
    while [ ${waited} -lt ${TERM_WAIT} ]; do
        local still_running=$(pgrep -x "$exe_name" 2>/dev/null)
        if [ -z "$still_running" ]; then
            echo "[OK]   ${exe_name} 已退出 (等待 ${waited}s)"
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done

    # 超时强杀
    echo "[KILL] ${exe_name} 优雅退出超时，强制杀死"
    kill -KILL ${pids} 2>/dev/null
    return 0
}

echo "========================================="
echo " GameServer 停止脚本"
echo " 模式: ${MODE}"
echo "========================================="

# ========== 按依赖逆序停止 ==========
# 启动顺序: appmgr → dbmgr → login → game → space
# 停止顺序: space → game → login → dbmgr → appmgr

stop_proc "space"
stop_proc "game"
stop_proc "login"
stop_proc "dbmgr"
stop_proc "appmgr"

echo "========================================="
echo " 停服完成"
echo "========================================="
