#include "kwinbridge.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

const char *kKwinService = "org.kde.KWin";
const char *kKwinScriptingPath = "/Scripting";
const char *kKwinScriptingIface = "org.kde.kwin.Scripting";
const char *kPetService = "org.dshpet.Pet";
const char *kPetPath = "/Pet";
const char *kPetIface = "org.dshpet.Pet";

const char *kPersistentPlugin = "dshpet";
const char *kCommandPlugin = "dshpet-cmd";

// 命令脚本：切换 DSH 对话窗口（激活 ↔ 最小化）。
// 由客户端按需写入运行时目录并让 KWin 重新加载执行——
// KWin 脚本没有「外部触发某个函数」的入口，只能用这个方式单向驱动。
const char *kCmdToggle = R"JS(
function isDsh(w) {
    var rc = String(w.resourceClass || "");
    if (rc.indexOf("dsh-pet") >= 0) return false;
    if (!/harness/i.test(String(w.caption || ""))) return false;
    return /^(chrome|chromium|microsoft-edge|brave|vivaldi|opera|firefox)/i.test(rc);
}
function list() { return workspace.windowList ? workspace.windowList() : workspace.clientList(); }
function find() {
    var l = list(), fb = null;
    for (var i = 0; i < l.length; i++) {
        if (!isDsh(l[i])) continue;
        if (/^chrome-[a-z]+-default$/i.test(String(l[i].resourceClass))) return l[i];
        if (!fb) fb = l[i];
    }
    return fb;
}
var w = find();
if (w) {
    // 判据只看 minimized（对齐 Windows 版 IsIconic）。
    // 不能判 w.active：点鲸鱼时宠物窗口会成为活动窗口，DSH 永远被判"不在前台"
    // → 只会呼出、永远收不起来（2026-10-06 主实测反馈）。
    if (w.minimized) { w.minimized = false; workspace.activeWindow = w; print("dsh-pet: cmd restore"); }
    else { w.minimized = true; print("dsh-pet: cmd minimize"); }
} else {
    print("dsh-pet: cmd no dsh window");
}
)JS";

const char *kCmdActivate = R"JS(
function isDsh(w) {
    var rc = String(w.resourceClass || "");
    if (rc.indexOf("dsh-pet") >= 0) return false;
    if (!/harness/i.test(String(w.caption || ""))) return false;
    return /^(chrome|chromium|microsoft-edge|brave|vivaldi|opera|firefox)/i.test(rc);
}
function list() { return workspace.windowList ? workspace.windowList() : workspace.clientList(); }
var l = list(), fb = null;
for (var i = 0; i < l.length; i++) {
    if (!isDsh(l[i])) continue;
    if (/^chrome-[a-z]+-default$/i.test(String(l[i].resourceClass))) { fb = l[i]; break; }
    if (!fb) fb = l[i];
}
if (fb) { if (fb.minimized) fb.minimized = false; workspace.activeWindow = fb; print("dsh-pet: cmd activate"); }
)JS";

const char *kCmdClose = R"JS(
function isDsh(w) {
    var rc = String(w.resourceClass || "");
    if (rc.indexOf("dsh-pet") >= 0) return false;
    if (!/harness/i.test(String(w.caption || ""))) return false;
    return /^(chrome|chromium|microsoft-edge|brave|vivaldi|opera|firefox)/i.test(rc);
}
function list() { return workspace.windowList ? workspace.windowList() : workspace.clientList(); }
var l = list(), target = null;
for (var i = 0; i < l.length; i++) {
    if (!isDsh(l[i])) continue;
    if (/^chrome-[a-z]+-default$/i.test(String(l[i].resourceClass))) { target = l[i]; break; }
    if (!target) target = l[i];
}
if (target) { target.close(); print("dsh-pet: cmd close"); }
)JS";

// 请求/清除「任务栏注意」（KDE demandsAttention：任务栏条目高亮闪动）。
// 2026-10-10 主需求：等决策时除托盘闪外，任务栏也给一路提醒（任务栏更大更醒目）。
const char *kCmdAttentionBody = R"JS(
function isDsh(w) {
    var rc = String(w.resourceClass || "");
    if (rc.indexOf("dsh-pet") >= 0) return false;
    if (!/harness/i.test(String(w.caption || ""))) return false;
    return /^(chrome|chromium|microsoft-edge|brave|vivaldi|opera|firefox)/i.test(rc);
}
function list() { return workspace.windowList ? workspace.windowList() : workspace.clientList(); }
var ON = __ON__;
var l = list(), n = 0;
for (var i = 0; i < l.length; i++) {
    if (!isDsh(l[i])) continue;
    if (ON && l[i].active) continue;   // 正在看这个窗口 → 不打扰
    l[i].demandsAttention = ON;
    n++;
}
print("dsh-pet: cmd attention " + (ON ? "on" : "off") + " n=" + n);
)JS";


} // namespace

KWinBridge::KWinBridge(QObject *parent)
    : QObject(parent)
{
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    m_dir = runtime + QStringLiteral("/dsh-pet");
    QDir().mkpath(m_dir);
    m_kwinScriptPath = m_dir + QStringLiteral("/dshpet-kwin.js");
    m_cmdScriptPath = m_dir + QStringLiteral("/dshpet-cmd.js");

    m_busOk = QDBusConnection::sessionBus().isConnected();
}

KWinBridge::~KWinBridge()
{
    if (m_available)
        unloadScript(QString::fromLatin1(kPersistentPlugin));
}

void KWinBridge::install()
{
    if (!m_busOk) {
        qWarning("dsh-pet: session D-Bus 不可用，跳过 KWin 集成（置顶/定位/窗口切换将不可用）");
        return;
    }

    // 1. 注册服务，供 KWin 脚本回调
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(QString::fromLatin1(kPetService));
    bus.registerObject(QString::fromLatin1(kPetPath), this,
                       QDBusConnection::ExportAllSlots);

    // 2. 导出内嵌脚本到运行时目录
    QFile res(QStringLiteral(":/assets/dshpet-kwin.js"));
    if (!res.open(QIODevice::ReadOnly)) {
        qWarning("dsh-pet: 内嵌 KWin 脚本缺失");
        return;
    }
    writeScript(m_kwinScriptPath, QString::fromUtf8(res.readAll()));

    // 3. 注入 KWin
    const bool loaded = loadScript(m_kwinScriptPath, QString::fromLatin1(kPersistentPlugin));
    m_available = loaded;
    if (!loaded)
        qWarning("dsh-pet: KWin 脚本注入失败（非 KWin 会话？）宠物仍可用，但没有置顶/定位");
}

void KWinBridge::writeScript(const QString &path, const QString &body)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("dsh-pet: 无法写入脚本 %s", qPrintable(path));
        return;
    }
    f.write(body.toUtf8());
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool KWinBridge::loadScript(const QString &path, const QString &pluginName)
{
    if (!m_busOk)
        return false;
    QDBusInterface iface(QString::fromLatin1(kKwinService),
                         QString::fromLatin1(kKwinScriptingPath),
                         QString::fromLatin1(kKwinScriptingIface),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return false;
    iface.setTimeout(3000);
    QDBusReply<int> reply = iface.call(QStringLiteral("loadScript"), path, pluginName);
    if (!reply.isValid()) {
        qWarning("dsh-pet: loadScript 失败: %s", qPrintable(reply.error().message()));
        return false;
    }
    iface.call(QStringLiteral("start"));
    return true;
}

void KWinBridge::unloadScript(const QString &pluginName)
{
    if (!m_busOk)
        return;
    QDBusInterface iface(QString::fromLatin1(kKwinService),
                         QString::fromLatin1(kKwinScriptingPath),
                         QString::fromLatin1(kKwinScriptingIface),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return;
    iface.setTimeout(3000);
    iface.call(QStringLiteral("unloadScript"), pluginName);
}

void KWinBridge::runCommand(const QString &command)
{
    if (!m_available) {
        emit dshWindowMissing();
        return;
    }
    writeScript(m_cmdScriptPath, command);
    unloadScript(QString::fromLatin1(kCommandPlugin));
    loadScript(m_cmdScriptPath, QString::fromLatin1(kCommandPlugin));
}

void KWinBridge::toggleDshWindow()
{
    if (!m_dshExists) {
        // 还没有对话窗口 → 交给上层拉起 PWA/浏览器
        emit dshWindowMissing();
        return;
    }
    runCommand(QString::fromLatin1(kCmdToggle));
}

void KWinBridge::activateDshWindow()
{
    if (!m_dshExists) {
        emit dshWindowMissing();
        return;
    }
    runCommand(QString::fromLatin1(kCmdActivate));
}

void KWinBridge::closeDshWindow()
{
    if (!m_available || !m_dshExists)
        return;
    runCommand(QString::fromLatin1(kCmdClose));
}

void KWinBridge::setAttention(bool on)
{
    if (!m_available || !m_dshExists)
        return;
    runCommand(QString::fromLatin1(kCmdAttentionBody)
                   .replace(QStringLiteral("__ON__"),
                            on ? QStringLiteral("true") : QStringLiteral("false")));
}


void KWinBridge::DshWindowState(bool exists, bool minimized, bool active)
{
    m_dshExists = exists;
    m_dshMinimized = minimized;
    emit dshWindowState(exists, minimized, active);
}

void KWinBridge::setAttentionMode(const QString &mode)
{
    m_attentionMode = mode;
}

QString KWinBridge::AttentionMode() const
{
    return m_attentionMode;
}
