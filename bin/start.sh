#!/bin/bash
#
# 服务器启动脚本
# 读取 res/engine.yaml 配置，启动对应的进程
# allinone 和 robot 不启动
# 有 apps 配置的进程启动多个实例，参数加 -sid=xxx
#

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_DIR=$(dirname "$SCRIPT_DIR")
YAML_FILE="${PROJECT_DIR}/res/engine.yaml"
BIN_DIR="${SCRIPT_DIR}"

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

# 用 python3 解析 YAML，输出格式: process_name sid（无 apps 时 sid 为空）
parse_yaml() {
    python3 -c "
import yaml, sys

with open('${YAML_FILE}', 'r') as f:
    config = yaml.safe_load(f)

skip = ['allinone', 'robot']
suffix = 'd' if '${MODE}' == 'debug' else ''

for proc_name, proc_conf in config.items():
    if proc_name in skip:
        continue
    
    if 'apps' in proc_conf and proc_conf['apps']:
        for app in proc_conf['apps']:
            print(f'{proc_name} {app[\"id\"]}')
    else:
        print(f'{proc_name}')
"
}

# 检查 python3 是否可用
if ! command -v python3 &> /dev/null; then
    echo "Error: 需要 python3 来解析 YAML 配置"
    exit 1
fi

# 检查 python3 yaml 模块是否可用
if ! python3 -c "import yaml" &> /dev/null; then
    echo "Error: 需要 PyYAML 模块，请运行: pip3 install pyyaml"
    exit 1
fi

# 启动进程
PIDS=()

while IFS= read -r line; do
    # 解析行：格式为 "process_name" 或 "process_name sid"
    proc_name=$(echo "$line" | awk '{print $1}')
    sid=$(echo "$line" | awk '{print $2}')
    
    exe_name=$(get_exe_name "$proc_name")
    exe_path="${BIN_DIR}/${exe_name}"
    
    # 检查可执行文件是否存在
    if [ ! -f "$exe_path" ]; then
        echo "[SKIP] ${exe_name} - 可执行文件不存在"
        continue
    fi
    
    # 构建启动参数
    args=""
    if [ -n "$sid" ]; then
        args="-sid=${sid}"
    fi
    
    echo "[START] ${exe_name} ${args}"
    "$exe_path" $args &
    PIDS+=($!)
done < <(parse_yaml)

echo "========================================="
echo " 已启动 ${#PIDS[@]} 个进程"
echo " PID 列表: ${PIDS[*]}"
echo "========================================="

# 等待所有子进程（脚本会一直阻塞，直到所有进程退出）
# 如果不需要阻塞等待，注释掉下面这行即可
wait
