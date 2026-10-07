#!/usr/bin/env bash
# 安装 dsh-pet-client 到用户目录（全程无需 sudo）
#   可执行文件 → ~/.local/bin/dsh-pet-client
#   应用菜单项 → ${XDG_DATA_HOME:-~/.local/share}/applications/dsh-pet.desktop
#   模式切换脚本 → ~/.dsh/scripts/dsh-mode-linux.sh
#   图标       → ~/.local/share/icons/hicolor/256x256/apps/dsh-pet.png
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
BIN="$PREFIX/bin"
APPS="$DATA_HOME/applications"
ICONS="$DATA_HOME/icons/hicolor/256x256/apps"
DSH_SCRIPTS="$HOME/.dsh/scripts"

# 1. 先构建
BUILD_DIR="${BUILD_DIR:-$HERE/build}" bash "$HERE/scripts/build.sh"

# 2. 安装可执行文件
install -Dm755 "$HERE/build/dsh-pet-client" "$BIN/dsh-pet-client"

# 3. 模式切换脚本（Linux 原生：systemd user 单元，无需 sudo）
install -Dm755 "$HERE/scripts/dsh-mode-linux.sh" "$DSH_SCRIPTS/dsh-mode-linux.sh"

# 4. 图标（从鲸鱼像素素材生成，避免额外的设计资源）
if command -v magick >/dev/null 2>&1; then
  mkdir -p "$ICONS"
  magick -size 80x80 -depth 8 "bgra:$HERE/assets/fish_blue.bin" \
         -resize 256x256 "$ICONS/dsh-pet.png"
elif command -v convert >/dev/null 2>&1; then
  mkdir -p "$ICONS"
  convert -size 80x80 -depth 8 "bgra:$HERE/assets/fish_blue.bin" \
          -resize 256x256 "$ICONS/dsh-pet.png"
fi

# 5. 应用菜单项
mkdir -p "$APPS"
cat > "$APPS/dsh-pet.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=DSH Tray
Name[zh_CN]=DSH 托盘
Comment=DeepSeek Harness floating whale (status indicator)
Comment[zh_CN]=DeepSeek Harness 托盘状态指示器
Exec=$BIN/dsh-pet-client
Icon=dsh-pet
Terminal=false
Categories=Utility;
StartupNotify=false
EOF
chmod 644 "$APPS/dsh-pet.desktop"

command -v update-desktop-database >/dev/null 2>&1 && \
  update-desktop-database "$APPS" >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
  gtk-update-icon-cache -qtf "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true

echo
echo "安装完成："
echo "  可执行文件 : $BIN/dsh-pet-client"
echo "  菜单项     : $APPS/dsh-pet.desktop"
echo "  模式脚本   : $DSH_SCRIPTS/dsh-mode-linux.sh"
echo
echo "启动：  $BIN/dsh-pet-client          # 首次运行会自动注册开机自启"
echo "        $BIN/dsh-pet-client --no-autostart   # 不想自启时"
echo "卸载：  rm -f $BIN/dsh-pet-client $APPS/dsh-pet.desktop \\"
echo "             ${XDG_CONFIG_HOME:-$HOME/.config}/autostart/dsh-pet-client.desktop"
