// DSH Pet Client —— KWin 常驻脚本
// 由 dsh-pet-client 在启动时通过 D-Bus（org.kde.KWin /Scripting loadScript）注入。
//
// 为什么需要它：纯 Wayland 下客户端无权激活别的应用窗口（wmctrl/xdotool 失效），
// 也无法可靠判断某个窗口的最小化/前台状态。这些只能由合成器脚本代劳。
//
// 职责：监视 DSH 对话窗口状态，变化时回调客户端 D-Bus
//       org.dshpet.Pet /Pet DshWindowState(bool exists, bool minimized, bool active)
//       —— 客户端据此决定托盘状态灯的基色（可见=蓝 / 收起=黑）与"回复完成"的停靠回执
//
// 注：2026-10-06 主决定移除桌面悬浮鲸鱼窗口，本脚本原先承担的
//     「宠物窗口置顶 + 贴屏幕右下角」（keepAbove / frameGeometry）职责已随之删除。

var DSH_CAPTION = /harness/i;                           // 页面标题固定含 Harness
var DSH_CLASS = /^(chrome|chromium|microsoft-edge|brave|vivaldi|opera|firefox)/i;
var PWA_CLASS = /^chrome-[a-z]+-default$/i;             // PWA 独立窗口：chrome-<appid>-Default

function isDsh(w) {
    if (!DSH_CAPTION.test(String(w.caption || ""))) return false;
    return DSH_CLASS.test(String(w.resourceClass || ""));
}

function windowList() {
    return workspace.windowList ? workspace.windowList() : workspace.clientList();
}

function findDsh() {
    var list = windowList();
    var fallback = null;
    for (var i = 0; i < list.length; i++) {
        var w = list[i];
        if (!isDsh(w)) continue;
        if (PWA_CLASS.test(String(w.resourceClass))) return w;   // PWA 独立窗口优先
        if (!fallback) fallback = w;
    }
    return fallback;
}

function report() {
    var w = findDsh();
    callDBus("org.dshpet.Pet", "/Pet", "org.dshpet.Pet", "DshWindowState",
             w ? true : false,
             w ? w.minimized : true,
             w ? w.active : false);
}

function hook(name, fn) {
    try { if (workspace[name]) workspace[name].connect(fn); } catch (e) {}
}

print("dsh-pet: kwin script loaded, windows=" + windowList().length);

hook("windowAdded", function () { report(); });
hook("windowRemoved", function () { report(); });
hook("windowActivated", function () { report(); });
hook("windowMinimizedChanged", function () { report(); });

report();
