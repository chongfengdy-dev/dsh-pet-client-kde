#!/usr/bin/env bash
# dsh-mode（Linux 原生版）：一键切换 dsh web 纯净/完整模式
#
# 用法: dsh-mode-linux.sh clean   切纯净模式（剥离第三方插件，保留官方 base+web-app）
#       dsh-mode-linux.sh full    切完整模式（默认，含第三方插件）
#       dsh-mode-linux.sh status  查看当前模式
#
# 原理: 创建/删除标记文件 ~/.dsh/.dsh-web-mode，然后重启 dsh-web 服务。
#       服务 ExecStart 由 web-launch.sh 读取该标记决定是否加 --patch。
#
# ⚠️ 与 Windows/WSL 侧 dsh-mode.sh 的区别（那份已随仓库拆分移除）：
#   WSL 版跑的是 **系统级** 单元，必须 sudo；Linux 桌面侧 dsh-web 是
#   **systemd user 单元**（~/.config/systemd/user/），无需 sudo。
#   用错版本会得到 "Unit dsh-web.service not found"。
#
# 用 stop→等端口释放→start 而非 restart：避免 Restart=always 在端口未释放时
# 反复重试、token 多次变化导致客户端跟不上（401）。
#
# 所有可变量都能用环境变量覆盖（不硬编码路径/端口/用户名）：
#   DSH_WEB_SERVICE（默认 dsh-web） DSH_WEB_PORT（默认 3080） DSH_PET_PORT（默认 3081）
set -euo pipefail

MARK="${HOME}/.dsh/.dsh-web-mode"
SERVICE="${DSH_WEB_SERVICE:-dsh-web}"
PORT="${DSH_WEB_PORT:-3080}"
PET_PORT="${DSH_PET_PORT:-3081}"

wait_port_free() {
  for _ in $(seq 1 40); do
    if ! (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then return 0; fi
    exec 3>&- 3<&- 2>/dev/null || true
    sleep 0.2
  done
  echo "警告: 端口 $PORT 释放超时，继续尝试启动"
}

switch_mode() {
  local mode="$1"
  echo "[1/3] 标记 $mode ..."
  if [ "$mode" = "clean" ]; then touch "$MARK"; else rm -f "$MARK"; fi
  echo "[2/3] 停止 $SERVICE ..."
  systemctl --user stop "$SERVICE" 2>/dev/null || true
  wait_port_free
  echo "[3/3] 启动 $SERVICE ..."
  systemctl --user start "$SERVICE"
  # 切换会重启 dsh web → 清宠物状态残留（切换前若正绿/橙会一直挂着）
  curl -s -m 2 -X POST "http://127.0.0.1:${PET_PORT}/api/pet-state" \
    -H 'Content-Type: application/json' -d '{"pet":"blue"}' >/dev/null 2>&1 || true
  echo "完成: 已切到 $mode 模式(端口 $PORT)"
}

case "${1:-}" in
  clean)  switch_mode clean ;;
  full)   switch_mode full ;;
  status)
    if [ -f "$MARK" ]; then
      echo "当前: 纯净模式 (剥离第三方插件)"
    else
      echo "当前: 完整模式 (含第三方插件)"
    fi
    ;;
  *)
    echo "用法: $(basename "$0") {clean|full|status}"
    exit 1
    ;;
esac
