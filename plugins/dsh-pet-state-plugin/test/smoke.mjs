// smoke.mjs — 不依赖 dsh 的本地验证：假 ctx + 假上报服务，检查状态机时序。
// 运行：node test/smoke.mjs
//
// 语义基准（2026-10-06 主定调）：
//   橙 = 只有「等待主决策」（授权/提问）时才闪；纯自动处理保持基色不打扰。
//   绿 = 一轮干完了。触发源是 `agent/status`（广播，可靠），**不是**
//        `agent/turn-stopping` —— 后者经 dispatch.serial 串行分发，根 ctx 收不到。
import http from "node:http";
import { apply, name } from "../lib/index.js";

const received = [];
const server = http.createServer((req, res) => {
	let body = "";
	req.on("data", (c) => (body += c));
	req.on("end", () => {
		received.push(JSON.parse(body || "{}"));
		res.writeHead(200, { "content-type": "application/json" });
		res.end(JSON.stringify({ ok: true }));
	});
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;

function makeCtx() {
	const handlers = new Map();
	return {
		on(event, fn) {
			if (!handlers.has(event)) handlers.set(event, []);
			handlers.get(event).push(fn);
			return () => {};
		},
		emit(event, ...args) {
			for (const fn of handlers.get(event) ?? []) fn(...args);
		},
		logger: { info: () => {} },
	};
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const pets = () => received.map((r) => r.pet);
const last = () => pets().at(-1);
const results = [];
const check = (label, cond) => {
	results.push([label, cond]);
	console.log(`${cond ? "✅" : "❌"} ${label}`);
};

const ctx = makeCtx();
apply(ctx, { port, greenHoldMs: 300, settleMs: 20, timeoutMs: 500 });
const agentA = { id: "a" };
const agentB = { id: "b" };
const running = (agent) => ctx.emit("agent/status", { agent, status: "running" });
const idle = (agent) => ctx.emit("agent/status", { agent, status: "idle" });
const approval = (kind) => ctx.emit("session/event", {}, { type: kind });
await sleep(60);

// ---------- 1. 装载 ----------
check("装载后立刻上报 blue", last() === "blue");

// ---------- 2. 纯自动处理不打扰 ----------
running(agentA);
await sleep(60);
check("纯自动处理（agent running）保持 blue、不打扰", last() === "blue");

// ---------- 3. 一轮结束 → green ----------
idle(agentA);
await sleep(80);
check("一轮干完（全部 agent 空闲）→ green", last() === "green");
check("green 载荷带 doneAt", typeof received.at(-1).doneAt === "number");
await sleep(420);
check("greenHold 到期 → blue", last() === "blue");

// ---------- 4. 等待授权 → orange ----------
approval("approval/asked");
await sleep(60);
check("等待授权 → orange", last() === "orange");
const orangeCount = pets().length;

approval("approval/asked");
await sleep(60);
check("重复授权请求强制再报 orange", pets().length === orangeCount + 1 && last() === "orange");

running(agentB);
await sleep(60);
check("授权未决期间其他 agent 活动不改变颜色", last() === "orange");

// ---------- 5. 主决策完 → 立刻回基色 ----------
approval("approval/decided");
await sleep(60);
check("主决策完成 → 立刻回 blue（即使 agent 仍在跑）", last() === "blue");

// ---------- 6. 保险丝：approval/decided 丢失也不会卡在橙色 ----------
approval("approval/asked");
await sleep(60);
check("再次等待授权 → orange", last() === "orange");
idle(agentB); // 最后一个 agent 也空闲了（agentA 早在第 3 步已 idle）
await sleep(120);
check("保险丝：全部 agent 空闲后不再橙色（不会卡住）", last() !== "orange");
await sleep(420); // 让 green 保持期过去，避免干扰下一段

// ---------- 7. 多 agent：还有人跑着就不算干完 ----------
running(agentA);
running(agentB);
await sleep(60);
idle(agentB);
await sleep(120);
check("仍有 agent 在跑时不转 green", last() !== "green");
idle(agentA);
await sleep(120);
check("最后一个 agent 也空闲 → green", last() === "green");
await sleep(420);

// ---------- 8. 卸载 agent 不算「一轮干完」 ----------
ctx.emit("agent/disposed", { agent: agentA });
await sleep(80);
check("agent/disposed 不误报 green", last() === "blue");

// ---------- 场景 2：上报服务不可达时必须静默 ----------
const dead = makeCtx();
let threw = null;
try {
	apply(dead, { host: "127.0.0.1", port: 1, greenHoldMs: 200, timeoutMs: 200, settleMs: 10 });
	dead.emit("session/event", {}, { type: "approval/asked" });
	await sleep(120);
	dead.emit("agent/status", { agent: {}, status: "running" });
	dead.emit("agent/status", { agent: {}, status: "idle" });
	dead.emit("session/event", {}, { type: "approval/decided" });
	await sleep(300);
	dead.emit("dispose");
} catch (e) {
	threw = e;
}
check("上报服务不可达时静默不抛错", threw === null);

// ---------- 场景 3：disabled / 非法 URL 停用 ----------
apply(makeCtx(), { enabled: false });
apply(makeCtx(), { url: "not-a-url" });
check("enabled:false 与非法 URL 均安全退出", true);

const failed = results.filter(([, ok]) => !ok);
console.log(`\n${failed.length === 0 ? "✅ 全部通过" : `❌ ${failed.length} 项失败`} (${results.length} 项)`);
server.close();
process.exit(failed.length === 0 ? 0 : 1);
