#!/bin/bash
# 双模型手动管控脚本 (Orin NX 16GB 专用 - 资源优化版)

MODEL_DIR="/home/nvidia/llm-models"
LOG_DIR="$MODEL_DIR/logs"
LLAMA_BIN="/home/nvidia/llama.cpp/build/bin/llama-server"

# 模型文件
INTENT_MODEL="$MODEL_DIR/qwen2.5-1.5b-instruct-q4_k_m.gguf"
CHAT_MODEL="$MODEL_DIR/qwen3-1.7b.Q4_K_M.gguf"

start_intent() {
    if pgrep -f "$LLAMA_BIN.*8081" > /dev/null; then
        echo "[意图1.5B] 已在运行 (PID: $(pgrep -f "$LLAMA_BIN.*8081"))"
        return 0
    fi
    echo "[意图1.5B] 启动中..."
    nohup $LLAMA_BIN \
        -m "$INTENT_MODEL" \
        --host 127.0.0.1 --port 8081 \
        -c 1024 -ngl 99 \
        --threads 4 --cpu-mask 0x0F \
        --no-mmap --temp 0.0 \
        > "$LOG_DIR/intent.log" 2>&1 &
    
    sleep 2
    if pgrep -f "$LLAMA_BIN.*8081" > /dev/null; then
        echo "[意图1.5B] ✅ 启动成功 (Port: 8081)"
    else
        echo "[意图1.5B] ❌ 启动失败，查看 $LOG_DIR/intent.log"
        return 1
    fi
}

start_chat() {
    if pgrep -f "$LLAMA_BIN.*8080" > /dev/null; then
        echo "[闲聊1.7B] 已在运行 (PID: $(pgrep -f "$LLAMA_BIN.*8080"))"
        return 0
    fi
    echo "[闲聊1.7B] 启动中..."
    nohup $LLAMA_BIN \
        -m "$CHAT_MODEL" \
        --host 127.0.0.1 --port 8080 \
        -c 2048 -ngl 99 \
        --cache-type-k q4_0 --cache-type-v q4_0 \
        --flash-attn on \
        --threads 4 --cpu-mask 0x30 \
        --no-mmap --temp 0.7 \
        --repeat-penalty 1.1 \
        > "$LOG_DIR/chat.log" 2>&1 &
    
    sleep 3
    if pgrep -f "$LLAMA_BIN.*8080" > /dev/null; then
        echo "[闲聊1.7B] ✅ 启动成功 (Port: 8080)"
    else
        echo "[闲聊1.7B] ❌ 启动失败，查看 $LOG_DIR/chat.log"
        return 1
    fi
}

stop_all() {
    echo "停止所有 Qwen3 服务..."
    pkill -f "$LLAMA_BIN.*808[01]"
    sleep 2
    pgrep -f "$LLAMA_BIN.*808[01]" > /dev/null && pkill -9 -f "$LLAMA_BIN.*808[01]"
    echo "✅ 所有服务已停止"
}

status() {
    echo "=== Qwen3 双模型状态 ==="
    for port in 8081 8080; do
        name=$([ "$port" = "8081" ] && echo "意图(1.5B)" || echo "闲聊(1.7B)")
        pid=$(pgrep -f "$LLAMA_BIN.*$port")
        [ -n "$pid" ] && echo "  $name : ✅ PID:$pid Port:$port" || echo "  $name : ⏹ 已停止"
    done
    echo -e "\n=== 内存状态 ===" && free -h | grep -E "Mem|Swap"
}

case "$1" in
    start)   start_intent && start_chat ;;
    stop)    stop_all ;;
    restart) stop_all && sleep 2 && start_intent && start_chat ;;
    status)  status ;;
    intent)  start_intent ;;
    chat)    start_chat ;;
    *)       echo "用法: $0 {start|stop|restart|status|intent|chat}" ;;
esac
