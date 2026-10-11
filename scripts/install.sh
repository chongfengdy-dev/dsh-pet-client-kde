#!/usr/bin/env bash
# install.sh — 一键部署 DSH Pet（Linux / KDE Plasma）
#
# 做五件事：
#   1. 检查依赖（Qt6 运行时、构建工具、node/npm、python3）
#   2. 构建并安装托盘客户端（--no-client 可跳过）
#   3. 安装 terminal-server 的 npm 依赖（3081 服务）
#   4. 写 dsh profile 配置（插件 link: 声明 + bundles + pet-state 挂载）
#   5. 生成并启用 systemd user 单元（--no-services 可跳过）
#
# 用法：
#   ./install.sh                 完整部署
#   ./install.sh --no-build      已有编译好的二进制，跳过构建
#   ./install.sh --no-services   只装文件，不生成/启动服务
#   ./install.sh --no-client     只部署运行环境（3080/3081 + 插件）
#   ./install.sh --with-wechat   额外部署微信通道（可选组件）
#   ./install.sh --dry-run       只显示会做什么，不写入
#
# 全部路径动态推导（$HOME / %h），不硬编码用户名与家目录。
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"     # 仓库根
PREFIX="${PREFIX:-$HOME/.local}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
UNIT_DIR="$CONFIG_HOME/systemd/user"
PROFILES_DIR="${DSH_PROFILES_DIR:-$HOME/.dsh/profiles}"
BIN="$PREFIX/bin"

DO_BUILD=1; DO_SERVICES=1; DO_CLIENT=1; DO_WECHAT=0; DRY=0
for a in "$@"; do
  case "$a" in
    --no-build)    DO_BUILD=0 ;;
    --no-services) DO_SERVICES=0 ;;
    --no-client)   DO_CLIENT=0 ;;
    --with-wechat) DO_WECHAT=1 ;;
    --dry-run)     DRY=1 ;;
    -h|--help)     sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数: $a（--help 看用法）" >&2; exit 2 ;;
  esac
done

step() { printf '\n\033[1m▶ %s\033[0m\n' "$*"; }
ok()   { printf '  ✅ %s\n' "$*"; }
warn() { printf '  ⚠️  %s\n' "$*"; }
die()  { printf '\n\033[31m✗ %s\033[0m\n' "$*" >&2; exit 1; }
run()  { if [ "$DRY" = 1 ]; then printf '  [dry] %s\n' "$*"; else "$@"; fi; }

MISSING=()

# ─────────────────────────────────────────────────────────────
step "1/5 检查依赖"

if pkg-config --exists Qt6Widgets 2>/dev/null || command -v qmake6 >/dev/null 2>&1 \
   || ls /usr/lib/libQt6Widgets.so* >/dev/null 2>&1; then
  ok "Qt6（运行时 + 开发文件）"
else
  warn "未找到 Qt6 —— 客户端必需。Arch/CachyOS: sudo pacman -S --needed qt6-base"
  MISSING+=(qt6-base)
fi

for c in node npm; do
  if command -v "$c" >/dev/null 2>&1; then ok "$c ($($c --version 2>/dev/null | head -1))"
  else warn "缺少 $c"; MISSING+=("$c"); fi
done

if [ "$DO_BUILD" = 1 ]; then
  for c in cmake ninja g++; do
    if command -v "$c" >/dev/null 2>&1; then ok "$c"
    else warn "缺少 $c（构建客户端需要）"; MISSING+=("$c"); fi
  done
fi

for c in python3 make; do
  command -v "$c" >/dev/null 2>&1 && ok "$c（node-pty 兜底编译用）" || warn "缺少 $c（node-pty 如需现场编译会失败）"
done

if [ ${#MISSING[@]} -gt 0 ]; then
  warn "缺失项：${MISSING[*]}"
  warn "Arch/CachyOS 可执行：sudo pacman -S --needed ${MISSING[*]}"
  if [ "$DRY" = 0 ]; then die "请先补齐依赖再运行（或用 --dry-run 查看流程）"; fi
fi

# ─────────────────────────────────────────────────────────────
step "2/5 构建并安装托盘客户端"

if [ "$DO_CLIENT" = 0 ]; then
  warn "已按 --no-client 跳过"
else
  [ "$DO_BUILD" = 1 ] && run bash "$HERE/scripts/build.sh"

  SRC_BIN="$HERE/build/dsh-pet-client"
  if [ -f "$SRC_BIN" ]; then
    run install -Dm755 "$SRC_BIN" "$BIN/dsh-pet-client"
    ok "可执行文件 → $BIN/dsh-pet-client"
  else
    warn "未找到 $SRC_BIN（先构建一次，或去掉 --no-build）"
  fi

  if command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1; then
    ICONS="$DATA_HOME/icons/hicolor/256x256/apps"
    CONV="$(command -v magick || command -v convert)"
    run mkdir -p "$ICONS"
    run "$CONV" -size 80x80 -depth 8 "bgra:$HERE/assets/fish_blue.bin" -resize 256x256 "$ICONS/dsh-pet.png"
    ok "图标 → $ICONS/dsh-pet.png"
  else
    warn "缺 ImageMagick，跳过图标生成"
  fi

  APPS="$DATA_HOME/applications"
  run mkdir -p "$APPS"
  if [ "$DRY" = 0 ]; then
    cat > "$APPS/dsh-pet.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=DSH Tray
Name[zh_CN]=DSH 托盘
Comment=DeepSeek Harness tray status indicator
Comment[zh_CN]=DeepSeek Harness 托盘状态指示器
Exec=$BIN/dsh-pet-client
Icon=dsh-pet
Terminal=false
Categories=Utility;
StartupNotify=false
EOF
    chmod 644 "$APPS/dsh-pet.desktop"
  fi
  ok "菜单项 → $APPS/dsh-pet.desktop"

  run install -Dm755 "$HERE/scripts/dsh-mode-linux.sh" "$HOME/.dsh/scripts/dsh-mode-linux.sh"
  ok "模式切换 → ~/.dsh/scripts/dsh-mode-linux.sh"

  if command -v update-desktop-database >/dev/null 2>&1; then
    run update-desktop-database "$APPS" >/dev/null 2>&1 || true
  fi
fi

# ─────────────────────────────────────────────────────────────
step "3/5 安装 3081 终端服务依赖"

TS="$HERE/runtime/terminal-server"
if [ -f "$TS/package.json" ]; then
  if [ -d "$TS/node_modules" ] && [ "$DRY" = 0 ]; then
    ok "node_modules 已存在，跳过（要重装先删掉它）"
  elif [ "$DRY" = 1 ]; then
    printf '  [dry] (cd %s && npm install)\n' "$TS"
  else
    ( cd "$TS" && npm install --no-audit --no-fund )
    ok "已安装 terminal-server 依赖"
  fi
else
  warn "未找到 $TS/package.json"
fi

# ─────────────────────────────────────────────────────────────
step "4/5 写 dsh profile 配置"

if [ ! -d "$PROFILES_DIR" ]; then
  warn "未找到 dsh profile 目录（$PROFILES_DIR）"
  warn "请先安装并至少启动一次 dsh 本体：npm i -g @deepseek-ai/dsh"
else
  SYNC_ARGS=("--web")
  [ "$DO_WECHAT" = 1 ] && SYNC_ARGS=()
  run bash "$HERE/scripts/sync-plugins.sh" "${SYNC_ARGS[@]}"
  ok "插件已同步（web$([ "$DO_WECHAT" = 1 ] && echo " + wechat")）"

  if [ "$DRY" = 0 ]; then
    python3 - "$PROFILES_DIR" "$DO_WECHAT" <<'PY'
import json, os, sys
profiles, do_wechat = sys.argv[1], sys.argv[2] == "1"
plan = [("web", ["dsh-term-panels", "dsh-archive-sync", "dsh-web-token-sync"])]
if do_wechat:
    plan.append(("wechat", ["dsh-wechat"]))
for prof, names in plan:
    pkg = os.path.join(profiles, prof, "package.json")
    if not os.path.isfile(pkg):
        print(f"  ⚠️  跳过 profiles/{prof}（无 package.json）"); continue
    d = json.load(open(pkg, encoding="utf-8"))
    prof_cfg = d.setdefault("dsh", {}).setdefault("profile", {})
    bundles = prof_cfg.setdefault("bundles", [])
    added = [n for n in names if n not in bundles]
    bundles.extend(added)
    json.dump(d, open(pkg, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    open(pkg, "a", encoding="utf-8").write("\n")
    print(f"  ✅ profiles/{prof}: bundles {'新增 ' + ', '.join(added) if added else '无变化'}")
PY
  else
    printf '  [dry] 把插件名写进 profile 的 dsh.profile.bundles\n'
  fi

  PATCH="$PROFILES_DIR/web/cordis.patch.yml"
  if [ "$DO_CLIENT" = 1 ] && [ -d "$PROFILES_DIR/web" ]; then
    TARGET="$HERE/plugins/dsh-pet-state-plugin/lib/index.js"
    if [ "$DRY" = 0 ]; then
      [ -f "$PATCH" ] || touch "$PATCH"
      if grep -q 'dsh-pet-state-plugin' "$PATCH"; then
        python3 - "$PATCH" "$TARGET" <<'PY'
import re, sys
patch, target = sys.argv[1], sys.argv[2]
s = open(patch, encoding="utf-8").read()
new, _ = re.subn(r'(?m)^(\s*name:\s*).*dsh-pet-state-plugin.*$', lambda m: m.group(1) + target, s)
open(patch, "w", encoding="utf-8").write(new)
PY
        ok "pet-state 挂载路径已更新"
      else
        cat >> "$PATCH" <<EOF

# ── DSH Pet 托盘状态灯：等待授权点橙、回复完成点绿 ──
- insert:
    - id: pet-state
      name: $TARGET
      config: { host: 127.0.0.1, port: 3081, path: /api/pet-state, greenHoldMs: 0, timeoutMs: 800 }
EOF
        ok "pet-state 挂载已写入 cordis.patch.yml"
      fi
    else
      printf '  [dry] 在 %s 里挂载 pet-state\n' "$PATCH"
    fi
  fi
fi

# ─────────────────────────────────────────────────────────────
step "5/5 生成并启用 systemd user 单元"

if [ "$DO_SERVICES" = 0 ]; then
  warn "已按 --no-services 跳过"
else
  case "$HERE" in
    "$HOME"/*) REPO_EXPR="%h/${HERE#"$HOME"/}" ;;
    *)         REPO_EXPR="$HERE" ;;
  esac
  echo "  仓库路径写法：$REPO_EXPR"

  run mkdir -p "$UNIT_DIR"
  UNITS=(dsh-web dsh-terminal)
  [ "$DO_WECHAT" = 1 ] && UNITS+=(dsh-wechat)

  for u in "${UNITS[@]}"; do
    src="$HERE/runtime/systemd/$u.service"
    [ -f "$src" ] || { warn "缺模板 $src"; continue; }
    if [ "$DRY" = 0 ]; then
      sed "s#@REPO@#$REPO_EXPR#g" "$src" > "$UNIT_DIR/$u.service"
    fi
    ok "$u.service → $UNIT_DIR/"
  done

  if [ "$DRY" = 0 ]; then
    systemctl --user daemon-reload
    # 2026-10-11 主定：不设开机自启（服务与托盘都改手动启动）。
    # 需要自启时自行执行：systemctl --user enable dsh-web dsh-terminal
    systemctl --user restart "${UNITS[@]/%/.service}"
    ok "服务已启用并启动"
  else
    printf '  [dry] systemctl --user daemon-reload && restart %s\n' "${UNITS[*]}"
  fi
fi

# ─────────────────────────────────────────────────────────────
step "完成"
cat <<EOF
  托盘客户端 : $BIN/dsh-pet-client
  菜单项     : $DATA_HOME/applications/dsh-pet.desktop
  运行环境   : $HERE/runtime/
  插件       : $HERE/plugins/
  单元目录   : $UNIT_DIR

  启动客户端 : $BIN/dsh-pet-client
  （默认不注册开机自启；托盘右键菜单可启停 dsh 服务）

  改完插件源码后：
      bash $HERE/scripts/sync-plugins.sh
      systemctl --user restart dsh-web
EOF
