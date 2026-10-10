// dsh-pet-state-plugin — agent 状态 → 宠物四色上报
//
// 目标：
//   等待用户授权/提问 → orange（400ms 心跳闪烁，提醒主去决策）
//   主决策完成        → 立刻回基色
//   一轮回复完成      → green（未查看提示，greenHoldMs 后自动回 blue）
//   其余（含纯自动处理）→ blue（基色由客户端按对话窗口可见性决定蓝/黑）
//
// ⚠️ 2026-10-06 主定调（对齐 Windows 版实感）：
//   橙色**只**表示「需要主介入」。纯自动处理（agent running 但没在等授权）
//   保持基色、不打扰——初版把 running 也当成橙色，主反馈"一直在闪不消失"。
//   因此 agent/status 不再驱动颜色，只用于维护「是否全部空闲」的集合。
//
// 设计要点（详见同目录 README.md 的证据与行号）：
//  * 纯观察者：只订阅 cordis 事件，不参与任何 waterfall，不注入服务，零外部依赖
//    （qt-client 下解析不到 @deepseek-ai/*，因此**不 import 任何第三方包**）。
//  * 授权信号：会话事件 approval/asked → orange；approval/decided → 重新判定。
//  * 保险丝：全部 agent 空闲时强制清除「等待授权」标记——即使 approval/decided
//    事件丢失，也不会把状态灯永久卡在橙色。
//  * POST 失败一律静默，绝不影响主流程（超时 800ms，错误吞掉）。
//
// 配置（profile 的 cordis.patch.yml 该行 config）：
//   host/port/path  默认 127.0.0.1:3081 /api/pet-state（端口可配置）
//   url             整条 URL 覆盖（优先于 host/port/path）
//   enabled         默认 true
//   timeoutMs       默认 800
//   greenHoldMs     默认 8000（green 至少保持这么久再转 blue）
//   settleMs        默认 150（turn-stopping 后延迟判定的保险丝）
//   debug           默认 false（ctx.logger 可用时打印）
import http from "node:http";

export const name = "pet-state";

const DEFAULT_CONFIG = {
	enabled: true,
	host: "127.0.0.1",
	port: 3081,
	path: "/api/pet-state",
	url: "",
	timeoutMs: 800,
	greenHoldMs: 8000,
	settleMs: 150,
	debug: false,
};

/** 会被当成「等待用户决策」的会话事件类型（可扩展）。 */
const APPROVAL_ASKED = new Set(["approval/asked"]);
const APPROVAL_DECIDED = new Set(["approval/decided", "approval/cancelled", "approval/denied"]);

function numOr(value, fallback) {
	const n = Number(value);
	return Number.isFinite(n) && n >= 0 ? n : fallback;
}

function normalizeConfig(raw) {
	const cfg = { ...DEFAULT_CONFIG };
	if (raw && typeof raw === "object") {
		if (raw.enabled !== undefined) cfg.enabled = raw.enabled !== false;
		if (typeof raw.host === "string" && raw.host) cfg.host = raw.host;
		cfg.port = numOr(raw.port, cfg.port);
		if (typeof raw.path === "string" && raw.path) cfg.path = raw.path;
		if (typeof raw.url === "string") cfg.url = raw.url;
		cfg.timeoutMs = numOr(raw.timeoutMs, cfg.timeoutMs);
		cfg.greenHoldMs = numOr(raw.greenHoldMs, cfg.greenHoldMs);
		cfg.settleMs = numOr(raw.settleMs, cfg.settleMs);
		cfg.debug = raw.debug === true;
	}
	cfg.target = cfg.url || `http://${cfg.host}:${cfg.port}${cfg.path}`;
	return cfg;
}

export function apply(ctx, rawConfig = {}) {
	const cfg = normalizeConfig(rawConfig);
	if (!cfg.enabled) return;

	const log = (message) => {
		if (!cfg.debug) return;
		try {
			if (ctx.logger?.info) ctx.logger.info(`[pet-state] ${message}`);
			else console.log(`[pet-state] ${message}`);
		} catch { /* 忽略 */ }
	};

	/** 运行态：全部靠闭包保存（cordis 的 ctx 是代理对象，不能挂自定义属性）。 */
	const state = {
		running: new Set(), // 正在 running 的 agent（含 subagent）——仅用于判断「是否全部空闲」
		pendingApproval: false, // 有未决的授权/提问请求 → 橙色
		current: null, // 我们「意图」中的颜色（去重用）
		pendingGreen: false, // 刚结束一轮，等待判定为 green
		greenAt: 0, // 上次上报 green 的时间
		blueTimer: null,
		settleTimer: null,
		disposed: false,
	};

	let url;
	try {
		url = new URL(cfg.target);
	} catch {
		log(`非法 URL，插件停用: ${cfg.target}`);
		return;
	}

	/** 单次 POST；永不抛错、永不阻塞调用方。 */
	function post(pet) {
		return new Promise((resolve) => {
			let settled = false;
			const done = (ok) => {
				if (settled) return;
				settled = true;
				resolve(ok);
			};
			try {
				const payload = pet === "green"
					? { pet, doneAt: Date.now() }
					: { pet };
				const body = JSON.stringify(payload);
				const req = http.request(
					{
						host: url.hostname,
						port: url.port || 80,
						path: `${url.pathname}${url.search}`,
						method: "POST",
						headers: {
							"content-type": "application/json",
							"content-length": Buffer.byteLength(body),
						},
						timeout: cfg.timeoutMs,
					},
					(res) => {
						res.resume(); // 丢弃响应体，释放 socket
						res.on("end", () => done(res.statusCode >= 200 && res.statusCode < 300));
						res.on("error", () => done(false));
					},
				);
				req.on("timeout", () => {
					req.destroy();
					done(false);
				});
				req.on("error", () => done(false));
				req.end(body);
			} catch {
				done(false);
			}
		});
	}

	/** 上报一种颜色（意图级去重；失败则允许下次重试）。 */
	function setPet(pet, reason, force = false) {
		if (state.disposed) return;
		if (!force && state.current === pet) return;
		state.current = pet;
		log(`${pet} (${reason})`);
		post(pet).then((ok) => {
			if (!ok && state.current === pet) state.current = null; // 失败：允许后续重发
		});
	}

	function clearTimer(key) {
		if (state[key] !== null) {
			clearTimeout(state[key]);
			state[key] = null;
		}
	}

	function scheduleBlue() {
		clearTimer("blueTimer");
		const remain = Math.max(0, cfg.greenHoldMs - (Date.now() - state.greenAt));
		state.blueTimer = setTimeout(() => {
			state.blueTimer = null;
			sync("green-hold-expired");
		}, remain);
		if (state.blueTimer.unref) state.blueTimer.unref();
	}

	/**
	 * 颜色决策（优先级从上到下）：
	 *   1. 等待主决策（授权/提问）→ orange
	 *   2. 刚结束一轮           → green（保持 greenHoldMs）
	 *   3. 其余（含处理中）      → blue 基色
	 */
	function sync(reason, force = false) {
		if (state.disposed) return;

		// 正在确认「这一轮是否真的结束」——期间不做颜色决策，避免蓝闪一下再转绿
		if (state.settleTimer !== null) return;

		const now = Date.now();

		if (state.pendingApproval) {
			clearTimer("blueTimer");
			state.pendingGreen = false;
			state.greenAt = 0;
			setPet("orange", `${reason}:await-approval`, force);
			return;
		}

		if (state.pendingGreen) {
			state.pendingGreen = false;
			state.greenAt = now;
			setPet("green", `${reason}:turn-complete`, force);
			scheduleBlue();
			return;
		}

		if (state.greenAt !== 0) {
			// greenHoldMs <= 0 = 常绿模式（2026-10-06 主定调）：绿一直亮，
			// 直到用户把对话页面调出来、客户端回执（POST /api/pet-ack）转蓝。
			if (cfg.greenHoldMs <= 0) return;
			if (now - state.greenAt < cfg.greenHoldMs) {
				scheduleBlue(); // green 保持期内不覆盖
				return;
			}
		}

		setPet("blue", `${reason}:idle`, force);
	}

	/** 所有 agent 刚空闲下来 → 延迟 settleMs 再确认一轮是否真的结束（防抖）。 */
	function armGreen() {
		clearTimer("settleTimer");
		state.settleTimer = setTimeout(() => {
			state.settleTimer = null;
			if (state.running.size > 0 || state.pendingApproval) return; // 又忙起来了
			state.pendingGreen = true;
			sync("all-idle");
		}, cfg.settleMs);
		if (state.settleTimer.unref) state.settleTimer.unref();
	}

	// ---- 事件订阅（全部为只读观察，不参与 waterfall） ----

	// (a) 授权/审批：**唯一**让状态灯变橙的来源
	ctx.on("session/event", (_session, event) => {
		try {
			const type = event?.type;
			if (typeof type !== "string") return;
			if (APPROVAL_ASKED.has(type)) {
				state.pendingApproval = true;
				setPet("orange", `approval-asked(${type})`, true);
			} else if (APPROVAL_DECIDED.has(type)) {
				state.pendingApproval = false;
				sync("approval-decided");
			} else if (cfg.debug && /question|approval|permission|turn/i.test(type)) {
				log(`候选事件类型: ${type}`);
			}
		} catch { /* 忽略 */ }
	});

	// (b) agent 处理状态：不驱动橙色，但**负责触发绿色**
	//
	// ⚠️ 不能用 `agent/turn-stopping`：它经 `dispatch.serial(...)` 串行分发，
	//    根 ctx 上的普通监听器收不到（实测 green 从未触发过）。`agent/status`
	//    走的是广播 emit，实测可靠。语义等价：所有 agent 都空闲 = 这一轮干完了。
	ctx.on("agent/status", (payload) => {
		try {
			const agent = payload?.agent;
			if (payload?.status === "running") {
				if (agent) state.running.add(agent);
				clearTimer("settleTimer"); // 又忙起来了 → 取消待判绿
				// 新一轮开始：上一轮的 green 可能已被客户端回执（服务端转蓝），
				// 清掉意图缓存，免得这一轮结束时因去重而不上报
				state.current = null;
			} else {
				if (agent) state.running.delete(agent);
				if (state.running.size === 0) armGreen();
			}
			// 保险丝：全部空闲 ⇒ 授权请求必然已处理完（防 approval/decided 丢失）
			if (state.running.size === 0 && state.pendingApproval) {
				state.pendingApproval = false;
				log("保险丝：全部 agent 空闲，清除等待授权标记");
			}
			sync("status");
		} catch { /* 忽略 */ }
	});

	// (c) agent 卸载：只清集合，**不算「一轮干完」**（会话被卸载不是回复完成）
	ctx.on("agent/disposed", (payload) => {
		try {
			if (payload?.agent) state.running.delete(payload.agent);
			if (state.running.size === 0) state.pendingApproval = false;
			sync("agent-disposed");
		} catch { /* 忽略 */ }
	});

	// (e) 卸载时清理定时器
	ctx.on("dispose", () => {
		state.disposed = true;
		clearTimer("blueTimer");
		clearTimer("settleTimer");
	});

	setPet("blue", "init", true);
	log(`已启用 → ${cfg.target}（greenHold=${cfg.greenHoldMs}ms, timeout=${cfg.timeoutMs}ms）`);
}
