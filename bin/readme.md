# 启动两个模型
sudo bash /home/jetson/local_cmcc_robot/bin/llama-ctl.sh start

# 停止所有模型
sudo bash /home/jetson/local_cmcc_robot/bin/llama-ctl.sh stop

# 重启
sudo bash /home/jetson/local_cmcc_robot/bin/llama-ctl.sh restart

# 查看状态
bash /home/jetson/local_cmcc_robot/bin/llama-ctl.sh status

# 只启动意图模型
sudo bash /home/jetson/local_cmcc_robot/bin/llama-ctl.sh intent

# 只启动对话模型
sudo bash /home/jetson/local_cmcc_robot/bin/llama-ctl.sh chat