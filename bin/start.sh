#!/bin/bash
set -euo pipefail

# 固定绝对路径，避免从任意目录调用时行为不一致
APP_DIR="/home/nvidia/local_cmcc_robot/bin"
PROGRAM_NAME="robot_avvtn"
PROGRAM_PATH="${APP_DIR}/${PROGRAM_NAME}"
PID_FILE="${APP_DIR}/${PROGRAM_NAME}.pid"
LOG_DIR="/var/log/robot_avvtn"
LOG_FILE="${LOG_DIR}/robot_$(date +%Y%m%d).log"

prepare_env() {
    # 创建日志目录，并切到程序目录
    mkdir -p "${LOG_DIR}"
    cd "${APP_DIR}"

    # 启动前做基础检查，避免 nohup 后才报错
    if [ ! -x "${PROGRAM_PATH}" ]; then
        echo "启动失败：程序不存在或不可执行: ${PROGRAM_PATH}"
        exit 1
    fi

    # 与 service 配置保持一致，显式设置 ROS_DOMAIN_ID
    export ROS_DOMAIN_ID=0

    # 动态判断：无网卡可用时回退到 localhost 模式
    if ip -o link show up 2>/dev/null | grep -qv 'lo\|docker\|veth\|br-'; then
        : # 有可用网卡，不限制
    else
        export ROS_LOCALHOST_ONLY=1
    fi
}

stop_existing_instance() {
    # 幂等停止：未运行时 stop.sh 可能返回非 0，不阻断启动
    bash "${APP_DIR}/stop.sh" || true
}

start_new_instance() {
    # 后台启动 + 脱离终端，日志写入按天文件
    nohup "${PROGRAM_PATH}" >> "${LOG_FILE}" 2>&1 &

    local new_pid
    new_pid=$!

    # 记录 PID 供 stop.sh 优先精确停止
    echo "${new_pid}" > "${PID_FILE}"
    echo "$(date): ${PROGRAM_NAME} started with PID: ${new_pid}" >> "${LOG_FILE}"
    echo "启动成功，新进程 PID: ${new_pid}"
}

prepare_env
stop_existing_instance
start_new_instance
