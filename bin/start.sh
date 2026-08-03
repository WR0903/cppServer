#!/bin/bash
#
# 服务器启动脚本
# 读取 res/engine.yaml 配置，按依赖顺序启动对应的进程
# allinone 和 robot 不启动
# 有 apps 配置的进程启动多个实例，参数加 -sid=xxx
#
# 启动顺序：appmgr → dbmgr → login/game/space（依赖方需要先连上 appmgr 和 dbmgr）
#

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_DIR=$(dirname "$SCRIPT_DIR")
YAML_FILE="${PROJECT_DIR}/res/engine.yaml"
BIN_DIR="${SCRIPT_DIR}"
LOG_DIR="${PROJECT_DIR}/logs"

# 统一日志目录：stdout 重定向日志、log4cplus 业务日志都写到这里
mkdir -p "${LOG_DIR}"

# 固定工作目录为项目根目录，保证 log4 配置里 "./logs/xxx.log" 这类相对路径
# 无论从哪个目录调用本脚本，最终都落在同一个 logs 目录下
cd "${PROJECT_DIR}" || exit 1

# 每批启动后的等待秒数（让监听 socket 就绪后再启动依赖方）
BOOT_WAIT=2

# 判断当前是 Debug 还是 Release 模式
# Debug 模式可执行文件带 'd' 后缀，如 appmgrd, logind
# Release 模式不带 'd'，如 appmgr, login
MODE=""
if [ -f "${BIN_DIR}/appmgrd" ]; then
    MODE="debug"
elif [ -f "${BIN_DIR}/appmgr" ]; then
    MODE="release"
else
    echo "Error: 未找到任何可执行文件，请先编译项目"
    exit 1
fi

echo "========================================="
echo " GameServer 启动脚本"
echo " 模式: ${MODE}"
echo " 配置: ${YAML_FILE}"
echo "========================================="

# 获取可执行文件名（debug 加 'd' 后缀，release 不加）
get_exe_name() {
    local proc_name=$1
    if [ "${MODE}" = "debug" ]; then
        echo "${proc_name}d"
    else
        echo "${proc_name}"
    fi
}

# 检查 python3 是否可用
if ! command -v python3 > /dev/null 2>&1; then
    echo "Error: 需要 python3 来解析 YAML 配置"
    exit 1
fi

# 检查 python3 yaml 模块是否可用
if ! python3 -c "import yaml" > /dev/null 2>&1; then
    echo "Error: 需要 PyYAML 模块，请运行: pip3 install pyyaml"
    exit 1
fi

# 启动单个进程（后台运行，记录PID）
start_proc() {
    local proc_name=$1
    local sid=$2

    local exe_name=$(get_exe_name "$proc_name")
    local exe_path="${BIN_DIR}/${exe_name}"

    if [ ! -f "$exe_path" ]; then
        echo "[SKIP] ${exe_name} - 可执行文件不存在"
        return 1
    fi

    local args=""
    if [ -n "$sid" ]; then
        args="-sid=${sid}"
    fi

    local log_suffix="${proc_name}"
    if [ -n "$sid" ]; then
        log_suffix="${proc_name}_${sid}"
    fi

    echo "[START] ${exe_name} ${args}"
    # stdin 重定向到 /dev/null：后台进程读终端会收到 SIGTTIN 被挂起（卡死）
    # stdout/stderr 重定向到日志文件：避免多进程输出交叉混乱
    "$exe_path" $args < /dev/null > "${LOG_DIR}/stdout_${log_suffix}.log" 2>&1 &
    PIDS+=($!)
    return 0
}

# 从 YAML 解析某个服务的 apps 列表（输出每个 app 的 id）
parse_apps() {
    local proc_name=$1
    python3 -c "
import yaml
with open('${YAML_FILE}', 'r') as f:
    config = yaml.safe_load(f)
conf = config.get('${proc_name}', {})
apps = conf.get('apps', [])
if apps:
    for app in apps:
        print(app['id'])
"
}

# 从 YAML 解析某个服务的监听端口
parse_port() {
    local proc_name=$1
    python3 -c "
import yaml
with open('${YAML_FILE}', 'r') as f:
    config = yaml.safe_load(f)
conf = config.get('${proc_name}', {})
print(conf.get('port', 0))
"
}

# 等待某个端口被监听（最多等 WAIT_MAX 秒）
wait_for_port() {
    local port=$1
    local wait_max=${2:-10}
    local waited=0
    while [ $waited -lt $wait_max ]; do
        if ss -tln | grep -q ":${port} "; then
            echo "[OK] 端口 ${port} 已就绪 (等待 ${waited}s)"
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "[WARN] 端口 ${port} 在 ${wait_max}s 内未就绪，继续启动"
    return 1
}

# ========== 按依赖顺序启动 ==========

PIDS=()

# 第1批：appmgr（中心协调器，所有服务都需要连上它）
start_proc "appmgr"
if [ $? -eq 0 ]; then
    appmgr_port=$(parse_port "appmgr")
    if [ -n "$appmgr_port" ] && [ "$appmgr_port" -gt 0 ]; then
        wait_for_port "$appmgr_port" 10
    else
        sleep $BOOT_WAIT
    fi
fi

# 第2批：dbmgr（数据库管理，login/game/space 需要连上它）
start_proc "dbmgr"
if [ $? -eq 0 ]; then
    dbmgr_port=$(parse_port "dbmgr")
    if [ -n "$dbmgr_port" ] && [ "$dbmgr_port" -gt 0 ]; then
        wait_for_port "$dbmgr_port" 10
    else
        sleep $BOOT_WAIT
    fi
fi

# 第3批：login（依赖 appmgr + dbmgr）
while IFS= read -r sid; do
    start_proc "login" "$sid"
done < <(parse_apps "login")
sleep $BOOT_WAIT

# 第4批：game（依赖 appmgr + dbmgr + space）
while IFS= read -r sid; do
    start_proc "game" "$sid"
done < <(parse_apps "game")
sleep $BOOT_WAIT

# 第5批：space（依赖 appmgr + dbmgr）
while IFS= read -r sid; do
    start_proc "space" "$sid"
done < <(parse_apps "space")

echo "========================================="
echo " 已启动 ${#PIDS[@]} 个进程"
echo " PID 列表: ${PIDS[*]}"
echo "========================================="

# 等待所有子进程（脚本会一直阻塞，直到所有进程退出）
# 如果不需要阻塞等待，注释掉下面这行即可

