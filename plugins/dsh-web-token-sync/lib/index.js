// dsh-web-token-sync：dsh web 启动后，把带一次性 token 的认证 URL 写入客户端的
// token 文件，让 pet client 每次启动都能读到最新 token，免手动维护。
//
// 背景：dsh web 0.1.2-rc.1 起每次进程启动生成一次性 launch token（仅内存，
// 打印在启动 URL 里）。本插件在进程内调 connection.authenticatedUrl() 拿到与
// 本进程一致的 token URL（同 root、同 launch token），落盘给客户端读。
//
// **跨平台（2026-10-06 改造）**：同一份代码覆盖两类部署——
//   · WSL / Windows：扫桌面 DSH-Pet-Client* 运行目录，各写一份 dsh-web-token.txt
//   · Linux 原生  ：写 ~/.dsh/dsh-web-token.txt（+ ~/.local/bin 兜底），
//                    顺序与 qt-client/src/dshlauncher.cpp 的 tokenFileCandidates() 一致
// 改造原因：原实现第一步就找 /mnt/c/Users，在原生 Linux 上必然抛错、又被自己的
// try/catch 吞掉 → 静默空转，token 永远写不出来（2026-10-06 主发现）。
//
// 挂载：web profile 的 package.json 已在 bundles 里声明本包，无需额外 patch 行。
// ⚠️ profile 内是**复制品**而非符号链接 → 改完源码要跑 nim-client/sync-plugin.sh
//    同步过去（或手动复制 lib/），否则 dsh 加载的仍是旧版。
import { mkdirSync, readdirSync, statSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";

/** 稳定插件名（cordis loader 用） */
export const name = "dsh-web-token-sync";

/** 依赖服务：connection（含 authenticatedUrl）与 webServer（取监听端口） */
export const inject = ["connection", "webServer"];

/** WSL 里解析 Windows 用户目录：/mnt/c/Users/<用户名>/（排除系统内置与隐藏目录） */
function winUserHome() {
	try {
		const users = "/mnt/c/Users";
		const entries = readdirSync(users).filter(
			(d) =>
				!["Public", "Default", "Default User", "All Users"].includes(d) &&
				!d.startsWith(".") &&
				d !== "desktop.ini"
		);
		return entries.length > 0 ? path.join(users, entries[0]) : null;
	} catch {
		return null; // 原生 Linux 没有 /mnt/c → 由调用方跳过 Windows 分支
	}
}

/** Linux 原生：客户端会去读的 token 文件位置（顺序与 dshlauncher.cpp 一致） */
function linuxTargets() {
	const home = os.homedir();
	const files = [];
	if (process.env.DSH_WEB_TOKEN_FILE) files.push(process.env.DSH_WEB_TOKEN_FILE);
	files.push(path.join(home, ".dsh", "dsh-web-token.txt"));
	files.push(path.join(home, ".local", "bin", "dsh-web-token.txt"));
	return files;
}

/**
 * 桌面下所有 DSH 客户端运行目录（名字以 DSH-Pet-Client 开头且内含
 * dsh_client_full.exe，兼容 v2.1.x 与 v2.2 起的多版本并存），全部写入——
 * 用户实际运行哪个副本都能读到。
 */
function clientDirs(home) {
	const desktop = path.join(home, "Desktop");
	try {
		const out = [];
		for (const name of readdirSync(desktop)) {
			const p = path.join(desktop, name);
			if (!name.startsWith("DSH-Pet-Client")) continue;
			let isDir = false;
			try {
				isDir = statSync(p).isDirectory();
			} catch {}
			if (!isDir) continue;
			try {
				if (readdirSync(p).includes("dsh_client_full.exe")) out.push(p);
			} catch {}
		}
		return out;
	} catch {
		return [];
	}
}

/** 写一份 token 文件（失败只告警，绝不影响主流程） */
function writeOne(ctx, file, authUrl) {
	try {
		mkdirSync(path.dirname(file), { recursive: true });
		// 0600：token 等同凭证，别让同机其他用户读到
		writeFileSync(file, `${authUrl}\n`, { encoding: "utf8", mode: 0o600 });
		ctx.logger?.info?.(`[dsh-web-token-sync] 已写入认证 URL -> ${file}`);
	} catch (e) {
		ctx.logger?.warn?.(`[dsh-web-token-sync] 写入 ${file} 失败: ${e?.message ?? e}`);
	}
}

export function apply(ctx, config = {}) {
	try {
		const port = ctx.webServer?.port;
		if (port === undefined) throw new Error("webServer.port 不可用");
		const authUrl = ctx.connection.authenticatedUrl(`http://127.0.0.1:${port}`);

		// 目标一：Linux 原生
		const files =
			typeof config?.targetFile === "string" && config.targetFile
				? [config.targetFile]
				: linuxTargets();
		for (const file of files) writeOne(ctx, file, authUrl);

		// 目标二：WSL / Windows（桌面上的客户端运行目录）；找不到就静默跳过
		const winHome = winUserHome();
		if (winHome) {
			const dirs =
				typeof config?.targetDir === "string" && config.targetDir
					? [config.targetDir]
					: clientDirs(winHome);
			for (const dir of dirs) writeOne(ctx, path.join(dir, "dsh-web-token.txt"), authUrl);
		}
	} catch (error) {
		ctx.logger?.warn?.(
			`[dsh-web-token-sync] 写入失败: ${error instanceof Error ? error.message : String(error)}`
		);
	}
}
