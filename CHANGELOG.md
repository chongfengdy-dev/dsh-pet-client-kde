# 改动记录

> 客户端发布版本见 GitHub Releases：
> https://github.com/chongfengdy-dev/dsh-pet-client-kde
>
> dsh **运行环境**组件（终端服务、dsh 插件、启动包装）的改动不在这里，
> 见 `../dsh-runtime/CHANGELOG.md`。

## v1.1（2026-10-10）

本版新增/修复（详见下方各条）：

| 类别 | 内容 |
|:--|:--|
| **仓库形态** | 对齐 Windows 版「一整套」：仓库自带 `plugins/`（5 个 dsh 插件）与 `runtime/`（3081 服务、启动包装、systemd 模板）；新增 `安装.sh` 一键部署 |
| **任务栏提醒** | 审批时与托盘同步闪烁、回复完成时常亮（KWin `demandsAttention`）|
| **修复** | 启动误拉 PWA（多开窗口）；`dsh-web-token-sync` 跨平台化；konsole 拉起失败（图形环境轮询）|
| **改名** | 启动器/自启项「DSH 桌面鲸鱼」→「DSH 托盘」|

---

## v1.0 发布之后的逐条记录（2026-10-07 起）

### 2026-10-10 定稿：任务栏提醒（主实测确认）

**最终行为**（与托盘联动）：

| 状态 | 托盘 | 任务栏条目 |
|:--|:--|:--|
| 审批（橙）+ 窗口不在前台 | 400ms 闪烁 | **同步闪烁**（客户端每 400ms 反转 `demandsAttention`）|
| 回复完成（绿）+ 窗口不在前台 | 常亮 | **常亮**（置位一次）|
| 空闲 / 窗口在前台 | 蓝或黑 | 不提醒 |

实现：`main.cpp` 维护 `m_attentionMode`（off/blink/steady）+ `m_attentionTimer`(400ms)；
`KWinBridge::setAttention()` 走命令脚本改窗口属性。

**⚠️ 一个被推翻的结论（重要，别再"推理"掉）**：
中途我曾看日志里 on/off 交替、便断定「KDE 会忽略 off，反复切换只会常亮」，
于是把 400ms 定时器停掉改成"只置位一次"——**结果反而完全不闪了**。
主实测：**400ms 反转 `demandsAttention` 是真会闪的**（KDE 跟随属性变化重绘）。
**教训：这种视觉效果以实测为准，不要凭日志推断就改行为。**

**另外试过并否决的方案**：
- 页面标题加状态圆点（🟠/🟢）—— 主认为不够醒目，已移除
- KDE 的 `demandsAttention` 高亮色由主题决定，**改不了配色**（无法实现"绿色边框"）
- 查过系统：Plasma 6 没有「请求注意时闪烁」的开关，KWin/kdeglobals 里都无相关键
### 2026-10-10 追加：任务栏提醒（主需求：任务栏条目比托盘大，更醒目）

- **任务栏条目高亮**：等决策（橙）且窗口不在前台时，置 KWin 的 `demandsAttention`，
  任务栏条目高亮 —— 由客户端 `QTimer`(400ms) 驱动
  - 实现：`main.cpp` 维护 `m_attentionMode`（off/blink/steady）→ `KWinBridge::setAttention()`
  - ⚠️ 踩过的坑一：把轮询写进 **KWin 脚本**里 —— 脚本引擎**没有 setInterval/timer**，
    代码静默失效（任务栏完全不亮）。闪烁必须由**客户端定时**驱动。
  - ⚠️ 踩过的坑二：跟着窗口 `active` 电平反复下发 → `demandsAttention` 会把窗口提示出来，
    又触发 active 变化 → **死循环抖动**。改为只在「模式变化」时下发。
- **任务栏标题圆点**（真正能"变色"的手段）：页面标题按状态加前缀
  - 🟠 = 等你决策 / 🟢 = 回复完成 / 无 = 空闲
  - 文件：`plugins/dsh-term-panels/lib/client.js`（原先把标题锁死为 "DeepSeek Harness"）
  - 安全：识别 DSH 窗口的规则是「标题含 harness」，加前缀不影响
- **绿色不再点亮任务栏**（方案 C）：KDE 的 `demandsAttention` 高亮色由主题决定（橙），
  改不了配色，且**置位后不理会反复切换 → 闪不起来**；故绿色只靠标题 🟢 提示，不打扰。
- **顺带修掉一个启动缺陷**：客户端启动时无条件调 `activateDshWindow()`，而 KWin 脚本
  首次状态上报慢于 1.2s，会被误判成「没有对话窗口」→ **多开一个 PWA**。
  改为延迟 3s + 先看客户端侧真实状态 + 确实没有才拉起。

### 2026-10-10 补充：微信通道故障复盘

- **现象**：给 bot 发指令（如「大a」）没有任何回复，但 `dsh-wechat.service` 显示 active
- **诊断**：日志只有启动行、无消息处理痕迹；`~/.dsh/sessions/` 无新会话活动；
  进程 `wchan=do_epoll_wait` 且 CPU 低位（空闲）→ **iLink 长轮询连接卡住且不自动重连**
- **诱因**：当天服务被反复重启（搬迁重启 → 关机 → 开机 → 手动重启），连接被折腾断
- **处置**：`systemctl --user restart dsh-wechat` → 重建长轮询，消息立即恢复
  （恢复后 CPU 升至 10%+，`sessions/` 下出现 `session-wechat-*` 写入）
- **以后遇到「微信突然没反应」先试这一下**

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
