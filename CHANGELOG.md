# 改动记录

> 客户端发布版本见 GitHub Releases：
> https://github.com/chongfengdy-dev/dsh-pet-client-kde
>
> dsh **运行环境**组件（终端服务、dsh 插件、启动包装）的改动不在这里，
> 见 `../dsh-runtime/CHANGELOG.md`。

## v1.0 发布之后（2026-10-07 起）

### 2026-10-10（对齐 Win 版的「一整套」形态）

- **仓库自带运行环境**：把原先散落在本机 `dsh-runtime/` 的组件收进仓库
  - `plugins/`：dsh-pet-state-plugin、dsh-term-panels、dsh-archive-sync、dsh-web-token-sync、dsh-wechat
  - `runtime/`：terminal-server（3081）、web-launch.sh、clean.patch.yml、sync-plugin.sh、systemd 单元模板
  - 移动后同步更新 3 个 systemd 单元与 2 个 profile 的引用路径
- **一键部署**：新增 `安装.sh`，重写 `scripts/install.sh`
  - 五步：查依赖 → 构建装客户端 → 装 3081 依赖 → 写 profile 配置 → 生成并启用服务
  - 参数：`--no-build` `--no-services` `--no-client` `--with-wechat` `--dry-run`
- **新增 `scripts/sync-plugins.sh`**：把 plugins/ 同步进 dsh profile（复制品机制）
- **新增 `scripts/pack-share.sh`**：发布打包 + 敏感串扫描
- **systemd 模板重写**：WSL 系统级（`<USER>` 占位 / `multi-user.target`）
  改为 Linux 用户级（`%h` / `default.target` / `@REPO@` 安装时替换）
- **文档**：README / 使用说明 的安装章节按新流程重写
- **.gitignore**：补上 node_modules、backgrounds、运行时状态文件

### 2026-10-07

- **启动器与自启项改名**：「DSH 桌面鲸鱼」→ **「DSH 托盘」**
  - `scripts/install.sh`（`Name` / `Name[zh_CN]`）
  - `src/autostart.cpp`（自启项 Comment）
  - `src/main.cpp`（自启项 Name → `DSH Tray`）
  - 已同步到本机已安装的 `~/.local/share/applications/dsh-pet.desktop`
    与 `~/.config/autostart/dsh-pet-client.desktop`
  - 顺带把描述里的「悬浮鲸鱼状态指示器」改为「托盘状态指示器」
    （悬浮宠物在 v1.0 已砍掉，描述过时）
