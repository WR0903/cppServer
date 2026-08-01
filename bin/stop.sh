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

# ========== 清理 Redis 残留的在线标志 ==========
# 停服时进程可能来不及清理 Redis 中的在线标志（TTL 6 分钟），
# 残留的标志会导致重启后玩家再次登录时被误判为"账号在线"而被踢。
# 清理的 key：
#   engine::online::login::<账号>   (login 进程写入)
#   engine::online::game::<账号>    (game 进程写入)
#   engine::token::<账号>           (登录 token)

REDIS_CLI=$(command -v redis-cli 2>/dev/null)
if [ -n "${REDIS_CLI}" ]; then
    echo "-----------------------------------------"
    echo " 清理 Redis 残留在线标志"
    echo "-----------------------------------------"

    # 清理 login 在线标志
    LOGIN_KEYS=$(redis-cli -h 127.0.0.1 -p 6379 --scan --pattern "engine::online::login::*" 2>/dev/null)
    if [ -n "${LOGIN_KEYS}" ]; then
        echo "${LOGIN_KEYS}" | xargs -r redis-cli -h 127.0.0.1 -p 6379 DEL >/dev/null 2>&1
        echo "[CLEAN] engine::online::login::*  (清除 $(echo "${LOGIN_KEYS}" | wc -l) 个)"
    else
        echo "[SKIP]  engine::online::login::*  (无残留)"
    fi

    # 清理 game 在线标志
    GAME_KEYS=$(redis-cli -h 127.0.0.1 -p 6379 --scan --pattern "engine::online::game::*" 2>/dev/null)
    if [ -n "${GAME_KEYS}" ]; then
        echo "${GAME_KEYS}" | xargs -r redis-cli -h 127.0.0.1 -p 6379 DEL >/dev/null 2>&1
        echo "[CLEAN] engine::online::game::*   (清除 $(echo "${GAME_KEYS}" | wc -l) 个)"
    else
        echo "[SKIP]  engine::online::game::*   (无残留)"
    fi

    # 清理 token 标志
    TOKEN_KEYS=$(redis-cli -h 127.0.0.1 -p 6379 --scan --pattern "engine::token::*" 2>/dev/null)
    if [ -n "${TOKEN_KEYS}" ]; then
        echo "${TOKEN_KEYS}" | xargs -r redis-cli -h 127.0.0.1 -p 6379 DEL >/dev/null 2>&1
        echo "[CLEAN] engine::token::*          (清除 $(echo "${TOKEN_KEYS}" | wc -l) 个)"
    else
        echo "[SKIP]  engine::token::*          (无残留)"
    fi
else
    echo "[WARN] 未找到 redis-cli，跳过 Redis 残留标志清理"
    echo "       可手动执行: redis-cli KEYS 'engine::online::*' | xargs redis-cli DEL"
fi

echo "========================================="
echo " 停服完成"
echo "========================================="
