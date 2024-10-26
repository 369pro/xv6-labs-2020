#!/bin/bash

# 启动 tmux 会话
tmux new-session -d -s debug_session

# 在第一个窗口运行 QEMU
tmux send-keys -t debug_session 'make CPUS=1 qemu-gdb' C-m

# 在第二个窗口运行 GDB
tmux new-window -t debug_session:1
tmux send-keys -t debug_session:1 'gdb-multiarch kernel/kernel' C-m

# 附加到 tmux 会话
tmux attach -t debug_session

