#!/bin/bash
set -u

APP_DIR="/home/nvidia/local_cmcc_robot/bin"
BUILD_DIR="/home/nvidia/local_cmcc_robot/build/src"
PROGRAM_NAME="robot_avvtn"
PROGRAM_PATH_BIN="${APP_DIR}/${PROGRAM_NAME}"
PROGRAM_PATH_BUILD="${BUILD_DIR}/${PROGRAM_NAME}"
PID_FILE="${APP_DIR}/${PROGRAM_NAME}.pid"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo_color() {
    echo -e "${1}${2}${NC}"
}

# 仅将以下两个可执行文件视为目标进程：
# 1) /home/nvidia/local_cmcc_robot/bin/robot_avvtn
# 2) /home/nvidia/local_cmcc_robot/build/src/robot_avvtn
is_target_process() {
    local pid="$1"
    local exe_path
    exe_path="$(readlink -f "/proc/${pid}/exe" 2>/dev/null || true)"
    [ -n "${exe_path}" ] && {
        [ "${exe_path}" = "${PROGRAM_PATH_BIN}" ] || [ "${exe_path}" = "${PROGRAM_PATH_BUILD}" ];
    }
}

add_pid_if_target() {
    local pid="$1"
    [ -z "${pid}" ] && return 0
    [[ "${pid}" =~ ^[0-9]+$ ]] || return 0
    is_target_process "${pid}" || return 0
    if [[ " ${TARGET_PIDS[*]} " != *" ${pid} "* ]]; then
        TARGET_PIDS+=("${pid}")
    fi
}

collect_target_pids() {
    TARGET_PIDS=()

    # 1) PID 文件优先：精准命中 start.sh 拉起的进程
    if [ -f "${PID_FILE}" ]; then
        local pid_from_file
        pid_from_file="$(tr -d '[:space:]' < "${PID_FILE}")"
        add_pid_if_target "${pid_from_file}"
    fi

    # 2) 扫描兜底：覆盖手工从 bin/build/src 执行 ./robot_avvtn 的场景
    while IFS= read -r pid; do
        add_pid_if_target "${pid}"
    done < <(pgrep -x "${PROGRAM_NAME}" 2>/dev/null || true)
}

print_target_processes() {
    echo_color "${BLUE}" "正在停止 ${PROGRAM_NAME}，匹配到 PID: ${TARGET_PIDS[*]}"
    for pid in "${TARGET_PIDS[@]}"; do
        ps -fp "${pid}" 2>/dev/null | awk 'NR==1 || NR==2'
    done
}

terminate_gracefully() {
    echo_color "${BLUE}" "发送终止信号..."
    for pid in "${TARGET_PIDS[@]}"; do
        kill "${pid}" > /dev/null 2>&1 || true
    done
}

wait_for_exit_and_collect_remaining() {
    local timeout=10
    local counter=0

    while [ "${counter}" -lt "${timeout}" ]; do
        REMAINING=()
        for pid in "${TARGET_PIDS[@]}"; do
            if ps -p "${pid}" > /dev/null 2>&1; then
                REMAINING+=("${pid}")
            fi
        done

        [ "${#REMAINING[@]}" -eq 0 ] && return 0

        echo_color "${YELLOW}" "等待进程退出... ($((timeout-counter)) 秒后强制终止)"
        sleep 1
        counter=$((counter + 1))
    done
}

force_kill_remaining() {
    if [ "${#REMAINING[@]}" -eq 0 ]; then
        return 0
    fi

    echo_color "${RED}" "优雅停止超时，发送 SIGKILL: ${REMAINING[*]}"
    for pid in "${REMAINING[@]}"; do
        kill -9 "${pid}" > /dev/null 2>&1 || true
    done
    sleep 1
}

# 主流程：收集目标 -> 停止 -> 清理
collect_target_pids
if [ "${#TARGET_PIDS[@]}" -eq 0 ]; then
    echo_color "${YELLOW}" "程序 ${PROGRAM_NAME} 未运行，无需停止。"
    rm -f "${PID_FILE}"
    exit 0
fi

print_target_processes
terminate_gracefully
REMAINING=()
wait_for_exit_and_collect_remaining
force_kill_remaining

rm -f "${PID_FILE}"
echo_color "${GREEN}" "程序 ${PROGRAM_NAME} 已成功停止！"
