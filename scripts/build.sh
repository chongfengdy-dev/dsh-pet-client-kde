#!/usr/bin/env bash
# 构建 dsh-pet-client（Linux Qt6 客户端）
# 依赖：qt6-base qt6-declarative(可选) cmake ninja gcc
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$HERE/build}"
JOBS="${JOBS:-$(nproc)}"

if ! command -v cmake >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  echo "缺少构建工具。Arch/CachyOS:  sudo pacman -S --needed cmake ninja" >&2
  exit 1
fi

cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C "$BUILD" -j "$JOBS"

echo
echo "构建完成: $BUILD/dsh-pet-client"
echo "运行:     $BUILD/dsh-pet-client --no-autostart"
