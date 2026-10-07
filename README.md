# DSH Pet — Linux 托盘状态灯（Qt6）

DeepSeek Harness 的状态指示器：**托盘图标 = 四色状态灯**，左键呼出/收起对话窗口。

> **2026-10-06 架构调整（主决定）**：移除桌面悬浮鲸鱼窗口。
> 它的功能（点一下呼出对话、显示状态）与托盘完全重复，还占桌面。
> 状态显示改由托盘图标独立承担；动画、泡泡、拖动、鼠标穿透等相关代码已删除。

```
托盘状态灯（蓝/黑/橙闪/绿） · 点击呼出对话 · 模式切换 · 单实例 · 开机自启
```

---

## 一、快速开始

```bash
# 构建
bash scripts/build.sh

# 直接运行（不改动系统）
./build/dsh-pet-client --no-autostart

# 安装到用户目录（无需 sudo）
bash scripts/install.sh
```

依赖：`qt6-base` `cmake` `ninja` `gcc`（Arch/CachyOS：`sudo pacman -S --needed cmake ninja qt6-base`）

| 参数 | 说明 |
|:--|:--|
| `--no-autostart` | 本次运行不注册开机自启 |
| `--help` / `--version` | 帮助 / 版本 |

首次运行会自动注册开机自启（`~/.config/autostart/dsh-pet-client.desktop`）。

> ⚠️ **不要从 dsh 的终端/子进程里直接启动它**（`dsh-subprocess-*.scope`）：
> 那个 scope 一收尾，systemd 会按 cgroup 把客户端一起杀掉。
> 正常使用走开机自启或应用菜单；手工调试可用
> `systemd-run --user --unit=dsh-pet-client --collect ~/.local/bin/dsh-pet-client`。

---

## 二、四色状态灯

状态来源：`http://127.0.0.1:3081/api/pet-state`（HTTP 优先，失败回落 `~/pet-state.json`）。

| 颜色 | 含义 | 表现 |
|:--|:--|:--|
| 蓝 | 空闲待命（对话窗口可见） | 常亮 |
| 黑 | 对话窗口已收起 / 未打开 | 常亮 |
| 橙 | 提问中 / 等待授权 | **400ms 心跳闪烁**（橙 ↔ 基色） |
| 绿 | 回复完成、用户尚未查看 | 常亮（2026-08-27 主定：绿不闪） |

「用户已看到回复」的判定：对话窗口存在、未最小化、且处于前台 → 客户端
POST `/api/pet-ack` → 服务端把 green 转回 blue。

> 📌 状态的**写入方**目前在 CachyOS 侧还缺一块：没有人会在「提问/等待授权」时写
> `orange`、也没人在「回复完成」时写 `green`。Windows 时代这个角色由页面脚本承担，
> 迁移时没带过来。补齐方案（dsh 侧状态上报插件）见第八节。

---

## 三、⚠️ Wayland 实现要点

纯 Wayland 下客户端无权激活别的应用窗口（`wmctrl`/`xdotool` 失效），
也无法可靠判断某个窗口的最小化/前台状态。这些交给**启动时自动注入的 KWin 脚本**：

| 需求 | 做法 |
|:--|:--|
| 切换对话窗口（收起↔呼出） | KWin 脚本 `workspace.activeWindow = w` / `w.minimized` |
| 判断对话窗口是否存在/收起/在前台 | KWin 脚本回调客户端 D-Bus |
| 识别对话窗口 | 标题含 `harness` 且 class 形如 `chrome-<appid>-Default`（PWA 优先） |
| 拉起对话界面 | 找 PWA 的 `.desktop` 用 `gio launch`；没有则回落到默认浏览器打开 3080 |

注入验证（`journalctl --user -u plasma-kwin_wayland`）：

```
dsh-pet: kwin script loaded, windows=6
dsh-pet: cmd activate      # 左键托盘点一下 → 呼出对话窗口
dsh-pet: cmd minimize
```

非 KDE 环境（GNOME 等）下注入会失败：状态灯仍可用（透明、动画、托盘、状态），
但点击托盘只能拉起浏览器，无法切换已开窗口。

---

## 四、代码结构

```
qt-client/
├── CMakeLists.txt
├── assets/
│   ├── fish_{blue,black,orange,green}.bin   # 四色图标像素（80×80 BGRA 预乘）
│   └── dshpet-kwin.js                       # 注入 KWin 的常驻脚本
├── src/
│   ├── main.cpp            # 装配 + 状态机（基色/橙闪/green 回执）
│   ├── petcolor.h          # 四色枚举
│   ├── tray.*              # 托盘状态灯（图标 + 闪烁 + 菜单）
│   ├── petstate.*          # 状态轮询（HTTP + 文件回退）与回执
│   ├── kwinbridge.*        # KWin 脚本注入、D-Bus 服务、命令脚本
│   ├── singleinstance.*    # QLocalServer 单实例
│   ├── autostart.*         # ~/.config/autostart
│   └── dshlauncher.*       # PWA 发现、浏览器回落、模式切换脚本定位
└── scripts/
    ├── build.sh
    ├── install.sh
    └── dsh-mode-linux.sh   # 纯净/完整模式切换（systemd **user** 单元，无需 sudo）
```

---

## 五、托盘菜单

| 交互 | 行为 |
|:--|:--|
| 左键单击图标 | 切换对话窗口：已开→收起 / 收起→呼出 / 没开→拉起 PWA |
| 右键 → 切换：完整模式 | 直接执行 `dsh-mode-linux.sh full`（Linux 原生脚本无需 sudo） |
| 右键 → 切换：纯净模式 | 同上 `clean` |
| 右键 → 退出 | 关闭对话窗口（PWA）后退出 |

找不到 Linux 原生脚本时会回退成「复制指令到剪贴板 + 弹窗」（兼容 WSL 版脚本，
它用 `sudo systemctl` 操作系统级服务，与桌面侧的 user 单元不同）。
用 `DSH_MODE_SCRIPT=/path/to/script` 可显式指定。

---

## 六、踩坑记录（改这份代码前务必先读）

1. **切换对话窗口的判据只能用「最小化状态」，不能用「是否在前台」。**
   点托盘/图标时焦点会变，若用 `w.active` 判断，DSH 永远被判"不在前台"→
   只会呼出、收不起来。对齐 Windows 版的 `IsIconic()` 判据即可。

2. **托盘要先 `setIcon()` 再 `show()`**，否则启动瞬间报
   `QSystemTrayIcon::setVisible: No Icon set`。

3. **KWin 脚本引擎没有 `Qt` 全局**（那是 QML 才有的）；矩形要写成对象字面量
   `w.frameGeometry = {x:…, y:…, width:…, height:…}`；`Qt.rect(...)` 会报
   `ReferenceError: Qt is not defined`。

4. **启动时不要 toggle 对话窗口**，否则窗口正开着会被最小化；
   启动应走「确保呼出」（没开就拉起、收起就呼出、已在前台不动）。

5. 客户端被 SIGKILL 时析构不执行，注入的 KWin 脚本会**留在 KWin 里**继续跑
   （同名 `loadScript` 不会重复加载新版）。调试时先手动
   `qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript dshpet`。

6. **别从 dsh 的 subprocess scope 里启动客户端**（见第一节的警告）。
   判断方法：`cat /proc/<pid>/cgroup` 看是不是 `dsh-subprocess-*.scope`。

---

## 七、已知限制

- 多显示器下 KWin 脚本按窗口所在屏判断，没有跨屏统一逻辑。
- 非 KWin 合成器（GNOME/Mutter、wlroots 系）没有等价脚本接口。
- 服务未就绪时点击托盘只会拉起浏览器（2 秒冷却内不重复开窗）；
  后续可加「等待 3080 就绪再打开」的重试。
- 客户端从 SIGKILL 恢复后，KWin 里可能残留旧脚本（见踩坑 5）。

---

## 八、状态上报联动（A 方案，2026-10-06 已落地）

`orange` / `green` 的写入方 = **dsh 侧插件** [`dsh-pet-state-plugin/`](dsh-pet-state-plugin/)，
零依赖（只 `import node:http`）、纯观察者（不参与任何 waterfall、失败一律静默）。

订阅的原生 cordis 钩子（**语义 2026-10-06 主定调，对齐 Windows 版实感**）：
橙色**只**表示「需要主介入」——纯自动处理（agent running）保持基色、不打扰；
主决策完立刻回基色。

| 事件 | 作用 |
|:--|:--|
| `approval/asked` | **唯一**让状态灯变橙的来源（等待授权/提问） |
| `approval/decided` | 立刻回基色 |
| `agent/turn-stopping` | 一轮结束 → `green`（带 `doneAt`，`greenHoldMs` 后回 `blue`） |
| `agent/status` | **不驱动颜色**，只用于维护「是否全部空闲」 |
| `dispose` | 清理定时器 |

**保险丝**：全部 agent 空闲时强制清除「等待授权」标记——即使 `approval/decided`
事件丢失，状态灯也不会永久卡在橙色。
（背景：初版把 `agent/status=running` 也当成橙色，主反馈"一直在闪不消失"，
遂改为事件驱动 + 保险丝。）

**挂载方式**（无需 pnpm、无需重启）：在 `~/.dsh/profiles/web/cordis.patch.yml` 末尾追加

```yaml
- insert:
    - id: pet-state
      name: <绝对路径>/dsh-pet-state-plugin/lib/index.js
      config: { host: 127.0.0.1, port: 3081, path: /api/pet-state, greenHoldMs: 8000, timeoutMs: 800 }
```

实测（2026-10-06）：profile patch 被 HMR 热加载，**没有重启 dsh-web、没有中断会话**；
审批期间服务端状态变为 `orange`、审批结束回 `blue`。

注意事项：
- **改插件源码 `lib/index.js` 必须重启 dsh-web**（HMR 只监视 patch，不监视源码）。
- `clean`（纯净模式）下这一行也会加载；若要在纯净模式禁用它，
  把 `- id: pet-state` + `disabled: true` 加进 `clean.patch.yml`。
- 插件的 `package.json` 刻意**不写 peerDependencies**：`dsh-app-boot` 的兼容性闸门
  在无 peerDependencies 时直接放行，写了反而可能因版本范围被判不兼容而静默禁用。

---

*环境交接与 Wayland 探测结论见 `~/deepseek-harness/qt-client-env.md`。*
