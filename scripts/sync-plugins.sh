#!/usr/bin/env bash
# sync-plugins.sh — 把仓库 plugins/ 里的 dsh 插件同步进 dsh profile
#
# ── 为什么是「复制」而不是「符号链接」 ────────────────────────────────
# dsh 以 ESM 加载插件，按**真实路径**解析依赖。若把 profile/node_modules/<插件>
# 做成指向本仓库的符号链接，Node 会从仓库目录往上找 node_modules，那里没有宿主
# 的 @deepseek-ai/*（它们只装在 profile 里），于是报 failed to import。
# 2026-09-30 实测确认，详见 CHANGELOG。
#
# ── 用法 ─────────────────────────────────────────────────────────────
#   ./sync-plugins.sh                 # 同步全部（web + wechat）
#   ./sync-plugins.sh --web           # 只同步 web profile 用的插件
#   ./sync-plugins.sh --wechat        # 只同步 wechat profile 用的插件
#   ./sync-plugins.sh --list          # 只列出会做什么，不实际写
#
# 全部路径动态推导，不硬编码用户名/家目录。
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"     # 仓库根
PLUGINS_SRC="$HERE/plugins"
PROFILES_DIR="${DSH_PROFILES_DIR:-$HOME/.dsh/profiles}"

# 插件 → 目标 profile 的归属（这是插件的用途分类，不是路径硬编码）
WEB_PLUGINS=(dsh-term-panels dsh-archive-sync dsh-web-token-sync)
WECHAT_PLUGINS=(dsh-wechat)
# 通过 cordis.patch.yml 的 insert 挂载、不进 node_modules 的插件
PATCH_PLUGINS=(dsh-pet-state-plugin)

DO_WEB=0; DO_WECHAT=0; DRY=0
for arg in "$@"; do
  case "$arg" in
    --web)    DO_WEB=1 ;;
    --wechat) DO_WECHAT=1 ;;
    --list)   DRY=1; DO_WEB=1; DO_WECHAT=1 ;;
    -h|--help)
      sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) echo "未知参数: $arg（用 --help 看用法）" >&2; exit 2 ;;
  esac
done
# 不带参数 = 全部
if [ "$DO_WEB" = 0 ] && [ "$DO_WECHAT" = 0 ]; then DO_WEB=1; DO_WECHAT=1; fi

say() { printf '  %s\n' "$*"; }
ok()  { printf '  ✅ %s\n' "$*"; }
warn(){ printf '  ⚠️  %s\n' "$*"; }

# copy_plugin <插件名> <目标 profile 名>
copy_plugin() {
  local name="$1" profile="$2"
  local src="$PLUGINS_SRC/$name"
  local dst="$PROFILES_DIR/$profile/node_modules/$name"

  if [ ! -d "$src" ]; then warn "$name 不存在于 plugins/，跳过"; return; fi
  if [ ! -d "$PROFILES_DIR/$profile" ]; then
    warn "profile 「$profile」不存在（$PROFILES_DIR/$profile），跳过 $name"; return
  fi

  if [ "$DRY" = 1 ]; then say "$name → profiles/$profile/node_modules/"; return; fi

  mkdir -p "$(dirname "$dst")"
  rm -rf "$dst"
  cp -r "$src" "$dst"
  ok "$name → profiles/$profile/"
}

# 更新 profile 的 package.json 里对应插件的依赖声明为相对 link:
update_pkg_json() {
  local profile="$1"; shift
  local pkg="$PROFILES_DIR/$profile/package.json"
  [ -f "$pkg" ] || { warn "无 $pkg，跳过依赖声明更新"; return; }

  if [ "$DRY" = 1 ]; then say "更新 profiles/$profile/package.json 的依赖声明"; return; fi

  python3 - "$pkg" "$HERE" "$@" <<'PY'
import json, os, sys
pkg_path, repo, names = sys.argv[1], sys.argv[2], sys.argv[3:]
d = json.load(open(pkg_path, encoding="utf-8"))
deps = d.setdefault("dependencies", {})
prof_dir = os.path.dirname(pkg_path)
changed = []
for n in names:
    rel = os.path.relpath(os.path.join(repo, "plugins", n), prof_dir)
    val = f"link:{rel}"
    if deps.get(n) != val:
        deps[n] = val
        changed.append(n)
# 保持 key 排序，diff 更干净
d["dependencies"] = dict(sorted(deps.items()))
json.dump(d, open(pkg_path, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
open(pkg_path, "a", encoding="utf-8").write("\n")
print("  ✅ 依赖声明已更新: " + (", ".join(changed) if changed else "（无变化）"))
PY
}

# 更新 cordis.patch.yml 里 pet-state 插件的绝对路径（该插件用 insert 挂载）
update_patch_paths() {
  local patch="$PROFILES_DIR/web/cordis.patch.yml"
  [ -f "$patch" ] || { warn "无 $patch，跳过 pet-state 路径更新"; return; }
  local target="$HERE/plugins/dsh-pet-state-plugin/lib/index.js"
  if ! grep -q 'dsh-pet-state-plugin' "$patch"; then
    say "patch 里没有 pet-state 条目（未挂载该插件），跳过"; return
  fi
  if [ "$DRY" = 1 ]; then say "更新 cordis.patch.yml 里 pet-state 的路径 → $target"; return; fi
  # 用 python 精确替换该行，避免 sed 的分隔符/转义问题
  python3 - "$patch" "$target" <<'PY'
import re, sys
patch, target = sys.argv[1], sys.argv[2]
s = open(patch, encoding="utf-8").read()
new, n = re.subn(r'(?m)^(\s*name:\s*).*dsh-pet-state-plugin.*$', lambda m: m.group(1) + target, s)
open(patch, "w", encoding="utf-8").write(new)
print(f"  {'✅' if n else '－'} pet-state 路径{'已更新' if n else '无变化'}")
PY
}

echo "仓库   : $HERE"
echo "profiles: $PROFILES_DIR"
if [ "$DRY" = 1 ]; then echo "模式   : 预览（不写入）"; fi
echo

if [ "$DO_WEB" = 1 ]; then
  echo "── web profile ──"
  for p in "${WEB_PLUGINS[@]}";    do copy_plugin "$p" web; done
  update_pkg_json web "${WEB_PLUGINS[@]}"
  echo "── 用 insert 挂载的插件 ──"
  for p in "${PATCH_PLUGINS[@]}"; do if [ "$DRY" = 1 ]; then say "$p（不复制，仅更新挂载路径）"; fi; done
  update_patch_paths
  echo
fi

if [ "$DO_WECHAT" = 1 ]; then
  echo "── wechat profile ──"
  for p in "${WECHAT_PLUGINS[@]}"; do copy_plugin "$p" wechat; done
  update_pkg_json wechat "${WECHAT_PLUGINS[@]}"
  echo
fi

if [ "$DRY" = 0 ]; then
  cat <<'TIP'
提示：
  · 插件是「复制品」——改完仓库源码要重跑本脚本，再重启对应服务才生效：
      systemctl --user restart dsh-web        # web profile 的插件
      systemctl --user restart dsh-wechat     # wechat profile 的插件
  · 托盘菜单里也有「重启 dsh 服务」相关项。
TIP
fi
