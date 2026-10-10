# dsh-pet-state-plugin — agent 状态 → 宠物四色上报

> 调研 + 骨架，2026-10-06。**只读调研**：未修改 `~/.dsh/` 与 `nim-client/`，未重启任何服务。
> 目标：agent 开始处理 / 等待授权 → `orange`；一轮回复完成 → `green`；空闲 → `blue`。
> 上报端点：`POST http://127.0.0.1:3081/api/pet-state`，body `{"pet":"orange"}`（端口可配置）。

环境实测：`dsh 0.2.0-rc.2`，Node v24.15.0。

---

## ① dsh 插件如何注册

**结论：是的，dsh 0.2 有可直接使用的钩子；无需包装 agent 服务、无需订阅 ACP。**

Cordis 插件形式（以真实插件 `nim-client/dsh-wechat` 为准）：

```js
// lib/index.js
export const name = "dsh-wechat";                       // 插件名
export const inject = ["agentDefaultModel", "agents", "sessions"];  // 需要的服务（可省略）
export function apply(ctx, config = {}) { /* 注册服务/订阅事件 */ }
```
证据：`nim-client/dsh-wechat/lib/index.js:16-17,230-240`。

`package.json` 里 `dsh.bundle.patch` 声明「作为 profile bundle 时要叠加的 patch 文件」——
证据：`dsh-wechat/package.json:23-27`；解析逻辑 `dsh-app-boot/lib/index.js:495-508`（`bundlePatchFiles` / `bundlePatchPaths`）。

`cordis.patch.yml` 是该 bundle 的 loader 层，形如：
```yaml
- insert:
    - id: wechat-channel
      name: dsh-wechat
      inject: [agentDefaultModel, agents, sessions]
      config: {}
```
证据：`dsh-wechat/cordis.patch.yml:6-12`。

profile 的插件树由 **多层 patch 依次叠加**组成：`package.json` 的 `dsh.profile.bundles` 顺序 → 各 bundle 自己的 `cordis.patch.yml` → profile 用户层 `~/.dsh/profiles/web/cordis.patch.yml` → `--patch` 覆盖层。
证据：`~/.dsh/profiles/web/cordis.yml:1-4`（注释）、`dsh-app-boot/lib/index.js:958-1000`（`loadProfileDirectory`）。

**关键发现（让挂载不必改 package.json、不必 pnpm install）**：
`dsh-app-boot/lib/index.js:3537-3545` `anchorInsertedPluginNames()` 会把 insert 行的 `name`
——若为**绝对路径**或以 `./` `../` 开头——改写成 `file://` URL；
`dsh-app-boot/lib/index.js:3693-3700` `HostResolvedRootInclude.import()` 对 `file:` 走原生 ESM 导入。
即 **insert 行可以直接指向磁盘上的入口文件**（`name: /abs/path/lib/index.js`）。

**热挂载**：`dsh-hmr` 默认监听 profile 的 `cordis.patch.yml` 与 `~/.dsh/cordis.patch.yml`
（`dsh-hmr/lib/index.js:354,375`）以及 profile `package.json`（`:376`），改动会 `reconcileProfilePatches` 实时重建插件树。
`hmr` 行在 base bundle 中默认启用（`dsh-base/cordis.patch.yml:28-31`），`root: []` 表示**只监听配置、不监听模块源码**。
→ 改 patch 文件热生效；改插件源码本身不热生效。

---

## ② 现成的钩子/事件（含文件:行号）

全部是 cordis 事件，`ctx.on(name, handler)` 即可订阅。根 ctx 上注册的监听器是**无 scope 标记**的，
按 `dsh-scope` 的语义「admits untagged listeners globally」（`dsh-scope/lib/types/index.d.ts:86-95`），
**能收到所有 agent（含 subagent）的事件**。

| 需求 | 事件 | 证据 | 语义 |
|:--|:--|:--|:--|
| (a) agent 进入处理中 / 回到空闲 | `agent/status` | `dsh-agent/lib/types/runtime-types.d.ts:243-255`（声明）、`dsh-agent-loop/lib/index.js:797-798`（发射）、`AgentStatus = 'idle'\|'running'` 同文件 `:90` | running ⇄ idle，**等待授权期间保持 running** |
| (a') 一轮开始 / 输入被认领 | `agent/inbox/claimed` | `runtime-types.d.ts:277` | 即将进入 turn |
| (b) 工具调用需要用户授权 | `approval/asked` / `approval/decided`（**会话日志事件**） | `dsh-user-approval/lib/types/types.d.ts:37,48` | 审批请求/结果的审计对 |
| (b') 审批瀑布（可拦截） | `approval/request`（waterfall） | `dsh-user-approval/lib/types/types.d.ts:81` | 「Return an outcome to claim the request or call `next()` to delegate」 |
| (b'') agent 向用户提问 | `user-questions/request`（waterfall） | `dsh-user-questions/lib/types/types.d.ts:145` | ask_user_question |
| (c) 一轮回复完成 | `agent/turn-stopping` | `runtime-types.d.ts:370-391`（serial，await 后提交边界） | 「The turn is about to close」 |
| (c') 回合真正关闭 | `turn/end`（会话事件，带 `reason`） | `dsh-session/lib/types/types.d.ts:273-276` | `TurnEndReason` |
| (c'') assistant 消息落地 | `assistant/message`（会话事件） | `dsh-session/lib/types/types.d.ts:330` | 完整消息 + usage |
| 会话事件总线（万能兜底） | `session/event` | `dsh-session/lib/types/index.d.ts:53-64` | 回调签名 `(session, event)`，post-commit fire-and-forget |
| 会话生命周期 | `session/created` / `session/disposed` / `session/flush` | `dsh-session/lib/types/index.d.ts:42,52,73` | |
| 工具执行瀑布 | `tools/pre-execute` / `tools/post-execute` | `dsh-tools/lib/types/index.d.ts:47,70` | 工具级拦截点 |
| agent 起始/错误/流 | `agent/created` / `agent/error` / `agent/assistant-stream` / `agent/pre-step` / `agent/request` | `runtime-types.d.ts:227,402,366,304,327` | |

补充：每个 agent 事件的 payload 都会**自动融合 `agent` 字段**（`dsh-agent/lib/index.js:242-246` 的 `agentEvents`），
所以 `agent/status` 能直接拿到是哪个 agent 在跑。

**重要辨析**：`dsh-authorization` 不是「工具授权」，它是**凭证获取（OAuth/登录）**的 seam
（`dsh-authorization/lib/types/index.d.ts:1-15`，只提供 `authorization/settled`，同文件 `:48`）。
「等用户授权」对应的是 `dsh-user-approval`（base bundle 行 id `approval`，`dsh-base/cordis.patch.yml:244-247`）。

### 另有「命令钩子」体系（Claude Code / Codex dialect）
`dsh-hook-protocol` + `dsh-hooks-claude-code` 支持在原生拦截点上跑外部命令，
支持的事件：`SessionStart / UserPromptSubmit / PreToolUse / PostToolUse / Stop / SubagentStart / SubagentStop`
（`dsh-hooks-claude-code/lib/index.js:14-20`，注册点 `:218,232,248,265,292,309,322`）；
审计事件 `hook/invoked` / `hook/result`（`dsh-hook-protocol/lib/types/types.d.ts:18,30`）。
**但它没有 Notification / PermissionRequest 事件**，所以只能近似（`UserPromptSubmit`→orange、`Stop`→green），
且需要在 settings 里配置 `configPath` + 一个可执行脚本，比原生插件笨重。列为备选方案。

---

## ③ 若无直接钩子时的间接办法（可行性排序）

本插件走的就是第 1 条，其余留作备选：

1. **订阅原生 cordis 事件（本方案，最稳）**：`agent/status` + `agent/turn-stopping` + `approval/asked`。
   纯观察、不参与 waterfall、零依赖、不影响主流程。
2. **订阅会话事件流**：`ctx.on("session/event")`，自己折叠 `turn/start` / `turn/end` / `approval/asked`。
   已验证 `dsh-wechat/lib/index.js:107-123` 用 `session.snapshotEvents(seq)` 做过同类折叠，可行但更啰嗦。
3. **接入 CC/Codex 命令钩子**：可行但事件集不含审批；需写外部脚本。
4. **包装 agent 服务**：`ctx.get("agents")` 后在 `agents.create/resume` 上包一层，或在 `agent/created`
   里拿到 agent 实例轮询 `agent.status`（`runtime-types.d.ts:147`）。比事件订阅脆弱（要管生命周期）。
5. **监听 ACP 消息**：`dsh-acp` / `dsh-acp-app` 是**对外协议适配层**，进程内已有更细的事件，不推荐。

---

## ④ 插件骨架

```
qt-client/dsh-pet-state-plugin/
├── package.json          # 无 peerDependencies（= 永不被版本兼容闸门拦下）
├── cordis.patch.yml      # 作为 bundle 使用时的 loader entry
├── lib/index.js          # 主入口（零第三方依赖，只 import node:http）
├── test/smoke.mjs        # 假 ctx + 假服务端的状态机验证（11 项全过）
└── README.md
```

### 状态机设计

```
agent/status = running            → orange      （等待审批/提问期间仍是 running → 天然覆盖）
session/event approval/asked      → orange      （额外强化，强制重发）
agent/turn-stopping               → 打「待转绿」标记
   全部 agent 均 idle 时          → green
   greenHoldMs(默认 8s) 到期仍空闲 → blue
agent/status = idle（无回合结束）  → blue
```
多 agent（含 subagent）聚合：只要还有任一 agent `running` 就是 orange，避免 subagent 结束时误闪绿。

关键代码（完整版见 `lib/index.js`）：

```js
export const name = "pet-state";

export function apply(ctx, rawConfig = {}) {
  const cfg = normalizeConfig(rawConfig);          // url/port/timeoutMs/greenHoldMs/settleMs
  if (!cfg.enabled) return;
  const state = { running: new Set(), current: null, pendingGreen: false, greenAt: 0, blueTimer: null, settleTimer: null, disposed: false };

  function post(pet) { /* node:http POST，timeout 后 destroy，error 一律吞掉 */ }
  function setPet(pet, reason, force = false) {
    if (!force && state.current === pet) return;   // 意图级去重
    state.current = pet;
    post(pet).then((ok) => { if (!ok && state.current === pet) state.current = null; }); // 失败允许重试
  }
  function sync(reason) {
    if (state.running.size > 0) { clearTimer("blueTimer"); state.pendingGreen = false; state.greenAt = 0; return setPet("orange", reason); }
    if (state.pendingGreen) { state.pendingGreen = false; state.greenAt = Date.now(); setPet("green", reason); return scheduleBlue(); }
    if (state.greenAt !== 0 && Date.now() - state.greenAt < cfg.greenHoldMs) return scheduleBlue();
    setPet("blue", reason);
  }

  ctx.on("agent/status", ({ agent, status }) => {
    if (status === "running") { if (agent) state.running.add(agent); } else if (agent) state.running.delete(agent);
    sync("status");
  });
  ctx.on("agent/turn-stopping", () => { state.pendingGreen = true; armSettle(); });
  ctx.on("session/event", (_session, event) => {
    if (event?.type === "approval/asked") setPet("orange", "approval-asked", true);
    else if (event?.type === "approval/decided") sync("approval-decided");
  });
  ctx.on("agent/disposed", ({ agent }) => { state.running.delete(agent); sync("disposed"); });
  ctx.on("dispose", () => { state.disposed = true; clearTimer("blueTimer"); clearTimer("settleTimer"); });

  setPet("blue", "init", true);
}
```

零依赖是**硬要求**：`qt-client/` 下解析不到 `@deepseek-ai/*`
（实测 `node -e "require.resolve('@deepseek-ai/schemastery')"` 失败），
所以既不能 `import z from "@deepseek-ai/schemastery"`，也不能 import 任何 dsh 包。
`package.json` 刻意**不写 `peerDependencies`**：`dsh-app-boot/lib/index.js` 的
`evaluatePluginCompatibility()` 在 `!Object.hasOwn(fields,"peerDependencies")` 时直接放行，
写了反而可能被版本闸门 disable。

### 验证

```
$ node test/smoke.mjs
✅ 装载后立刻上报 blue
✅ agent running → orange
✅ 等待授权 (approval/asked) 强制再报 orange
✅ 第二个 agent running 仍是 orange
✅ A 仍在跑时子 agent 结束不转 green
✅ 全部 idle 且一轮结束 → green
✅ green 载荷带 doneAt
✅ greenHold 到期 → blue
✅ agent/disposed 不误报
✅ 上报服务不可达时静默不抛错
✅ enabled:false 与非法 URL 均安全退出
✅ 全部通过 (11 项)
```

---

## ⑤ 在 web profile 里启用（确切步骤）

### 路线 A：绝对路径挂载（推荐 —— 不用 pnpm、不改 bundles、**无需重启**）

**唯一改动**：在 `~/.dsh/profiles/web/cordis.patch.yml` **末尾追加**（该文件是 YAML 数组）：

```yaml
- insert:
    - id: pet-state
      name: <绝对路径>/dsh-pet-state-plugin/lib/index.js
      config:
        host: 127.0.0.1
        port: 3081
        path: /api/pet-state
        greenHoldMs: 0
        timeoutMs: 800
        debug: false
```

- `name` 会被 `anchorInsertedPluginNames` 转成 `file://` URL（`dsh-app-boot/lib/index.js:3537-3545`）。
- **是否重启**：`dsh-hmr` 监听该 patch 文件（`dsh-hmr/lib/index.js:354,375`），
  保存后应**热生效，不重启**。⚠️ 这一点属于「未实测」，见下方不确定项；若保存后日志没有出现
  `pet-state` 相关行或宠物无反应，再走路线 B。
- **副作用**：`clean` 模式（`clean.patch.yml`）不含该行的 disable，所以纯净模式下也会加载。
  若要它跟随模式，把 `- id: pet-state / disabled: true` 加进 `clean.patch.yml`。

### 路线 B：正式 bundle（与 dsh-wechat 同构，需 pnpm + **必须重启**）

1. `~/.dsh/profiles/web/package.json` 的 `dependencies` 加：
   `"dsh-pet-state-plugin": "link:../../../deepseek-harness/qt-client/dsh-pet-state-plugin"`
2. 同文件 `dsh.profile.bundles` 数组里加一项：`"dsh-pet-state-plugin"`
3. 安装链接：`dsh plugin --profile web install`（内部走 pnpm）。
4. 按需在 `~/.dsh/profiles/web/cordis.patch.yml` 用 `- id: pet-state` + `config:` 覆盖端口。
5. **重启 dsh-web**（会中断当前会话）：
   `systemctl --user restart dsh-web`（或按现网习惯 `stop` → 等 3080 释放 → `start`）。
   ⚠️ 我没有执行任何 systemctl 命令，这一步留给主。
   （HMR 也可能在改 `package.json` 后热加载，见 `dsh-hmr/lib/index.js:376` `refresh(true)`，
   同样属于未实测项。）

### 验证挂载成功
```bash
# 实时看宠物状态（只读，不影响服务）
curl -s http://127.0.0.1:3081/api/pet-state
# 手动打一发 orange 验证端点本身可用（这是端点设计用途，非变更）
curl -s -X POST http://127.0.0.1:3081/api/pet-state -H 'content-type: application/json' -d '{"pet":"orange"}'
```
然后任意发一条对话，观察 3081 的 `/api/pet-state` 是否按 blue→orange→green→blue 变化。

### ⚠️ 唯一需要重启的操作汇总
| 操作 | 需要重启 dsh-web？ |
|:--|:--|
| 路线 A：只改 `profiles/web/cordis.patch.yml` | **预期不需要**（HMR 热加载，待实测） |
| 路线 B：改 `package.json` + `pnpm install` | 预期也不需要，但不确定；保守做法是重启 |
| 修改插件源码 `lib/index.js` | **需要**（`hmr.root: []`，源码不在监视范围） |

---

## ⑥ 不确定 / 需要实测验证的点

1. **`file://` 绝对路径 insert 能否真正 import 成功** —— 证据来自 `dsh-app-boot/lib/index.js:3693-3700`
   （`isAbsolute(name) → pathToFileURL`）与 `:3537-3545`（normalize），但 `file:` 走的是
   `internal.import(specifier, bareModuleBaseUrl, {})` 而非原生 `import()`；
   `:1641-1650` 的 `installRuntimeInterception` 对不在拦截层的 parent 会回落原生解析（看起来没问题），
   **但没有实机验证过**（验证＝写 patch 文件并观察，属于变更 `~/.dsh/`，本次被纪律禁止）。
2. **HMR 热挂载是否真的零重启生效** —— 证据：`dsh-hmr/lib/index.js:354-376` 明确 watch 了
   `profile.patchPath`；但「新增 insert 行（而非改 config）」是否能被 `reconcileProfilePatches`
   完整应用，未实测。
3. **`agent/status` 的 idle 时机** —— `dsh-agent-loop/lib/index.js:784-798` 的 `get status()` 把
   `maintenance` 阶段也算 idle。维护期（如压缩）可能短暂报 blue，未实测。
4. **等待审批期间 agent 是否稳定保持 `running`** —— 推断自 `phase.kind` 只有 idle/maintenance 才是 idle；
   审批发生在 step 内部，理应仍是 running。未实测；已用 `approval/asked` 强制补一次 orange 做兜底。
5. **green/blue 与现有 `pet-ack` 的交互** —— `nim-client/terminal-server/server.js:161-176` 显示
   term-panels 会在页面聚焦时 POST `/api/pet-ack` 把 green 转 blue。本插件默认 `greenHoldMs=8000`
   之后主动补 blue；若主希望「green 一直挂着直到用户查看」，把 `greenHoldMs` 调大（如 600000）。
6. **subagent 场景的观感** —— 本设计在「父 agent 等待 subagent」期间保持 orange（因为父仍 running）。
   若希望 subagent 干活时不闪橙，需要额外的 per-agent 过滤配置（当前未实现）。
7. **base 行 `approval` 的 policy 默认为 `ask`**（`dsh-base/cordis.patch.yml:244-247`，由
   `DSH_PERMISSION_MODE` 决定），因此 `approval/asked` 事件在默认部署下会产生。
8. **`dsh-client-ui-*` 的 web-ui-pet 插件**在 web profile 里被显式 `disabled: true`
   （`~/.dsh/profiles/web/cordis.patch.yml:19`），未找到其包体；它可能是 dshmarket 侧的历史宠物插件，
   与本方案无冲突，但说明「官方/市场侧已有过宠物联动」，可后续对照。
