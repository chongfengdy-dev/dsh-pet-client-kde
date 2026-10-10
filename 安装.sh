#!/usr/bin/env bash
# 一键安装入口 —— 等价于 `bash scripts/install.sh`
#
# 用法：
#   双击本文件（文件管理器里选「在终端中运行」），或
#   bash 安装.sh
#
# 可选参数直接透传给 install.sh，例如：
#   bash 安装.sh --with-wechat     额外部署微信通道
#   bash 安装.sh --dry-run         只看流程不写入
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$HERE/scripts/install.sh" "$@"
