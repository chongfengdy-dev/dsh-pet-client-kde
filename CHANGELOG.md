# 改动记录

> 客户端发布版本见 GitHub Releases：
> https://github.com/chongfengdy-dev/dsh-pet-client-kde
>
> dsh **运行环境**组件（终端服务、dsh 插件、启动包装）的改动不在这里，
> 见 `../dsh-runtime/CHANGELOG.md`。

## v1.0 发布之后（2026-10-07 起）

### 2026-10-07

- **启动器与自启项改名**：「DSH 桌面鲸鱼」→ **「DSH 托盘」**
  - `scripts/install.sh`（`Name` / `Name[zh_CN]`）
  - `src/autostart.cpp`（自启项 Comment）
  - `src/main.cpp`（自启项 Name → `DSH Tray`）
  - 已同步到本机已安装的 `~/.local/share/applications/dsh-pet.desktop`
    与 `~/.config/autostart/dsh-pet-client.desktop`
  - 顺带把描述里的「悬浮鲸鱼状态指示器」改为「托盘状态指示器」
    （悬浮宠物在 v1.0 已砍掉，描述过时）
