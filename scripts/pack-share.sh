#!/usr/bin/env bash
# pack-share.sh — 打发布包（DSH-Pet-Linux-<版本>-share.tar.gz）
#
# 设计原则（对齐 Windows 侧 pack-share.ps1）：
#   1. 只收「别人拿去能部署」的东西：源码、素材、插件、运行环境、脚本、文档
#   2. 排除构建/打包产物与私有数据：build/ dist/ experiments/
#      node_modules/（装时 npm i）  terminal-server/backgrounds/（本机背景图）
#   3. 打包前扫敏感串（绝对家目录、主机名、token），命中即中止、不产出包
#   4. 说明文档一律 .md
#
# 用法：
#   ./pack-share.sh 1.0                    # 输出到仓库 dist/
#   ./pack-share.sh 1.0 /path/to/output    # 指定输出目录
set -euo pipefail

VERSION="${1:?用法: pack-share.sh <版本号，如 1.0> [输出目录]}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="${2:-$HERE/dist}"
NAME="DSH-Pet-Linux-v$VERSION"
STAGE="$OUTDIR/$NAME"
ARCHIVE="$OUTDIR/$NAME-share.tar.gz"

echo "▶ 打包 $NAME"

# ── 1. 清扫旧产物 ────────────────────────────────────────────
rm -rf "$STAGE" "$ARCHIVE"
mkdir -p "$STAGE"

# ── 2. 收文件 ────────────────────────────────────────────────
INCLUDE=(src assets plugins runtime scripts CMakeLists.txt README.md 使用说明.md CHANGELOG.md LICENSE 安装.sh)
for item in "${INCLUDE[@]}"; do
  [ -e "$HERE/$item" ] || { echo "  ⚠️  缺少 $item，跳过"; continue; }
  cp -r "$HERE/$item" "$STAGE/"
  echo "  + $item"
done

# 二进制：已有编译产物就一并带上（主 2026-10-07 要求：附件含编译好的二进制）
if [ -f "$HERE/build/dsh-pet-client" ]; then
  install -Dm755 "$HERE/build/dsh-pet-client" "$STAGE/dsh-pet-client"
  echo "  + dsh-pet-client（已编译二进制，$(du -h "$STAGE/dsh-pet-client" | cut -f1)）"
else
  echo "  ⚠️  未找到 build/dsh-pet-client，包内将不含二进制（用户需自行构建）"
fi

# ── 3. 剔除不该发布的目录 ────────────────────────────────────
find "$STAGE" -type d \( -name node_modules -o -name build -o -name dist \
     -o -name experiments -o -name backgrounds -o -name .git \) -prune -exec rm -rf {} + 2>/dev/null || true
find "$STAGE" -type f \( -name '*.log' -o -name '*.zstd' -o -name 'term-state.json' \
     -o -name 'pet-state.json' -o -name '*.bak-*' \) -delete 2>/dev/null || true

# ── 4. 敏感串扫描（命中即中止）───────────────────────────────
echo "▶ 扫描敏感串"
if command -v grep >/dev/null 2>&1; then
  HITS=$(grep -rInE '/home/[a-z][a-z0-9_-]*|/Users/[A-Za-z]|DESKTOP-[A-Z0-9]{7}|ghp_[A-Za-z0-9]{20}|sk-[A-Za-z0-9]{20}' "$STAGE" 2>/dev/null \
         | grep -vE 'ImageMagick|/usr/(local/)?share|/home/<|pack-share\.sh|敏感串' || true)
  if [ -n "$HITS" ]; then
    echo "$HITS" | head -20 | sed 's/^/    /'
    echo
    echo "✗ 命中敏感串，已中止（未产出发布包）" >&2
    rm -rf "$STAGE"
    exit 1
  fi
  echo "  ✅ 未发现绝对家目录 / 主机名 / 凭证"
fi

# ── 5. 打包 ──────────────────────────────────────────────────
tar czf "$ARCHIVE" -C "$OUTDIR" "$NAME"
SIZE=$(du -h "$ARCHIVE" | cut -f1)
COUNT=$(tar tzf "$ARCHIVE" | grep -vc '/$' || true)

echo
echo "✅ 完成"
echo "   包    : $ARCHIVE"
echo "   大小  : $SIZE   文件数: $COUNT"
if [ -f "$STAGE/dsh-pet-client" ]; then echo "   二进制: 已包含"; else echo "   二进制: 未包含"; fi
