// DSH Pet —— Linux/Wayland 托盘状态灯
//
// 2026-10-06 主决定：移除桌面悬浮鲸鱼窗口（与托盘功能重复、还占桌面），
// 状态显示并入托盘图标：
//   · 托盘图标 = 四色状态灯（蓝=空闲 / 黑=对话窗口收起 / 橙=提问·待授权闪烁 / 绿=回复完成）
//   · 左键 = 切换对话窗口（收起↔呼出）；没有窗口则拉起 PWA / 浏览器
//   · 右键 = 菜单（完整/纯净模式切换、退出）
//
// 纯 Wayland 下客户端无权激活别的应用窗口，这部分仍由启动时注入的 KWin 脚本代劳。
#include "autostart.h"
#include "dshlauncher.h"
#include "kwinbridge.h"
#include "petstate.h"
#include "singleinstance.h"
#include "tray.h"

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDateTime>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>

namespace {
constexpr const char *kAppId = "dsh-pet-client";
constexpr const char *kDesktopFileName = "dsh-pet";   // → Wayland app_id（KWin 脚本据此识别）
constexpr int kLaunchCooldownMs = 2000;                // 防 2s 内重复开窗
constexpr int kStartupEnsureDelayMs = 1200;            // 启动后确保对话界面可用
} // namespace

class PetApp : public QObject
{
    Q_OBJECT

public:
    explicit PetApp(QObject *parent = nullptr);

    void run(bool enableAutostart);

    /// 第二个实例启动时（重复点击图标）→ 切换对话窗口
    void activateFromSecondInstance() { toggleDsh(); }

private:
    void applyColor();
    void maybeAck();
    void launchDsh();
    void toggleDsh();
    void onStateUpdated(PetColor color, qint64 doneAt);
    void onDshWindowState(bool exists, bool minimized, bool active);
    void onModeSwitch(const QString &mode);
    void setDshServices(bool start);
    void onQuit();

    Tray m_tray;
    PetState m_state;
    KWinBridge m_kwin;

    qint64 m_launchTick = 0;
    bool m_dshExists = false;
    bool m_dshMinimized = true;
    bool m_dshActive = false;
    bool m_remoteGreen = false;
    qint64 m_doneAt = 0;
    bool m_acked = false;
    bool m_ackEdge = false;   // 对话页面刚从后台切回前台（green 回执的边沿判据）
    bool m_stoppingServices = false;   // 主动停 dsh 服务期间：抑制"窗口没了→自动拉起"
    bool m_quitting = false;
};

PetApp::PetApp(QObject *parent)
    : QObject(parent)
{
    connect(&m_tray, &Tray::activateRequested, this, &PetApp::toggleDsh);
    connect(&m_tray, &Tray::modeSwitchRequested, this, &PetApp::onModeSwitch);
    connect(&m_tray, &Tray::dshServicesStartRequested, this,
            [this] { setDshServices(true); });
    connect(&m_tray, &Tray::dshServicesStopRequested, this,
            [this] { setDshServices(false); });
    connect(&m_tray, &Tray::quitRequested, this, &PetApp::onQuit);

    connect(&m_state, &PetState::updated, this, &PetApp::onStateUpdated);

    connect(&m_kwin, &KWinBridge::dshWindowState, this, &PetApp::onDshWindowState);
    connect(&m_kwin, &KWinBridge::dshWindowMissing, this, &PetApp::launchDsh);
}

void PetApp::run(bool enableAutostart)
{
    if (enableAutostart) {
        if (Autostart::install(kAppId, QStringLiteral("DSH Tray")))
            qInfo("dsh-pet: 已注册开机自启");
    }

    m_kwin.install();

    applyColor();
    m_state.start();

    // 启动即确保对话界面可用：窗口没开就拉起，收起就呼出，已在前台不动
    QTimer::singleShot(kStartupEnsureDelayMs, this, [this] { m_kwin.activateDshWindow(); });
}

void PetApp::applyColor()
{
    const bool visible = m_dshExists && !m_dshMinimized;
    m_tray.setBaseColor(visible ? PetColor::Blue : PetColor::Black);
    m_tray.setRemoteColor(m_state.color());
}

void PetApp::maybeAck()
{
    // green 回执（服务端据此转 blue）。2026-10-06 主定调：**绿要常亮**，
    // 直到用户把对话页面调出来。所以判据是「页面此刻就在前台」或「页面刚从
    // 后台切回前台（边沿）」——只用电平判断会把"一直开着页面"误当作已看到，
    // 绿还没看清就被掐掉。
    if (!m_remoteGreen || m_acked)
        return;
    if (!m_dshExists || m_dshMinimized)
        return;
    if (!m_dshActive && !m_ackEdge)
        return;
    m_ackEdge = false;
    m_acked = true;
    m_state.ack();
    qInfo("dsh-pet: green 已回执（对话页面回到前台）");
}

void PetApp::onStateUpdated(PetColor color, qint64 doneAt)
{
    const bool green = (color == PetColor::Green);
    if (green && doneAt != m_doneAt) {
        m_acked = false;      // 新一轮回复完成 → 重新提示
        m_ackEdge = false;    // 清掉上一轮遗留的"切回前台"边沿
    }
    if (!green)
        m_acked = false;
    m_remoteGreen = green;
    m_doneAt = doneAt;
    applyColor();
    maybeAck();
}

void PetApp::onDshWindowState(bool exists, bool minimized, bool active)
{
    const bool wasActive = m_dshActive;
    m_dshExists = exists;
    m_dshMinimized = minimized;
    m_dshActive = active;
    if (active && !wasActive)
        m_ackEdge = true;   // 用户把对话页面调出来了
    applyColor();
    maybeAck();
}

void PetApp::toggleDsh()
{
    // 有对话窗口 → KWin 脚本切换（收起↔呼出）；没有 → 拉起 PWA/浏览器
    m_kwin.toggleDshWindow();
}

void PetApp::launchDsh()
{
    if (m_stoppingServices)
        return;   // 是我们主动停的服务，别再自动把浏览器拉起来
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_launchTick < kLaunchCooldownMs)
        return;
    m_launchTick = now;
    QString how;
    DshLauncher::openDsh(&how);
    qInfo("dsh-pet: 拉起对话界面 -> %s", qPrintable(how));
}

void PetApp::onModeSwitch(const QString &mode)
{
    const DshLauncher::ModeScriptInfo info = DshLauncher::findModeScript();

    // Linux 原生脚本走 systemd user 单元，无需 sudo → 客户端直接执行，不用麻烦用户
    if (!info.path.isEmpty() && info.native) {
        auto *proc = new QProcess(this);
        proc->setProgram(QStringLiteral("bash"));
        proc->setArguments({info.path, mode});
        connect(proc, &QProcess::finished, this,
                [this, proc, mode](int code, QProcess::ExitStatus) {
                    const QString out =
                        QString::fromUtf8(proc->readAllStandardOutput()
                                          + proc->readAllStandardError()).trimmed();
                    m_tray.showMessage(
                        QStringLiteral("DSH 模式切换"),
                        code == 0
                            ? QStringLiteral("已切到 %1 模式\n%2").arg(mode, out)
                            : QStringLiteral("切换失败（退出码 %1）\n%2").arg(code).arg(out));
                    proc->deleteLater();
                });
        proc->start();
        m_tray.showMessage(QStringLiteral("DSH 模式切换"),
                           QStringLiteral("正在切换到 %1 模式…").arg(mode));
        return;
    }

    // 回退：需要 sudo 的环境（WSL 版脚本）→ 复制指令让用户到终端跑
    const QString cmd = DshLauncher::modeSwitchCommand(mode);
    if (cmd.isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("DSH 模式切换"),
                             QStringLiteral("未找到 dsh-mode 脚本。\n\n"
                                            "可用环境变量 DSH_MODE_SCRIPT 指定其绝对路径。"));
        return;
    }
    QApplication::clipboard()->setText(cmd);
    QMessageBox::information(
        nullptr, QStringLiteral("DSH 模式切换"),
        QStringLiteral("已复制以下指令到剪贴板，请在终端运行（遇到提权提示输入密码）：\n\n%1")
            .arg(cmd));
}

void PetApp::setDshServices(bool start)
{
    const QStringList units = DshLauncher::serviceUnits();
    if (units.isEmpty()) {
        m_tray.showMessage(
            QStringLiteral("DSH 服务"),
            QStringLiteral("未找到 dsh-* 用户服务单元（systemctl --user 不可用？）"));
        return;
    }

    if (!start)
        m_stoppingServices = true;   // 停服务会让对话窗口消失，别触发自动拉起

    auto *proc = new QProcess(this);
    QStringList args{QStringLiteral("--user"),
                     start ? QStringLiteral("start") : QStringLiteral("stop")};
    args << units;
    proc->setProgram(QStringLiteral("systemctl"));
    proc->setArguments(args);

    connect(proc, &QProcess::finished, this,
            [this, proc, start, units](int code, QProcess::ExitStatus) {
                const QString out =
                    QString::fromUtf8(proc->readAllStandardOutput()
                                      + proc->readAllStandardError()).trimmed();
                m_tray.showMessage(
                    QStringLiteral("DSH 服务"),
                    code == 0
                        ? QStringLiteral("%1：%2")
                              .arg(start ? QStringLiteral("已启动") : QStringLiteral("已退出"),
                                   units.join(QStringLiteral(" ")))
                        : QStringLiteral("操作失败（退出码 %1）\n%2").arg(code).arg(out));
                if (!start)
                    QTimer::singleShot(2000, this, [this] { m_stoppingServices = false; });
                proc->deleteLater();
            });
    proc->start();

    m_tray.showMessage(QStringLiteral("DSH 服务"),
                       start ? QStringLiteral("正在启动 dsh 全套服务…")
                             : QStringLiteral("正在退出 dsh 全套服务…"));
}

void PetApp::onQuit()
{
    if (m_quitting)
        return;
    m_quitting = true;
    // v2.2 语义：退出时一并关闭 dsh 对话窗口（PWA 独立窗口）
    m_kwin.closeDshWindow();
    QTimer::singleShot(400, qApp, &QCoreApplication::quit);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(kAppId));
    QApplication::setApplicationDisplayName(QStringLiteral("DSH Pet"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));
    QApplication::setOrganizationName(QStringLiteral("dsh"));
    QApplication::setDesktopFileName(QString::fromLatin1(kDesktopFileName));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("DeepSeek Harness 托盘状态灯（Linux/Wayland）"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption noAutostart(QStringLiteral("no-autostart"),
                                   QStringLiteral("本次运行不注册开机自启"));
    parser.addOption(noAutostart);
    parser.process(app);

    // 单实例：重复启动 → 通知老实例切换对话窗口后退出
    SingleInstance guard(QString::fromLatin1(kAppId));
    if (!guard.tryLock()) {
        guard.notifyExisting();
        return 0;
    }

    PetApp petApp;
    QObject::connect(&guard, &SingleInstance::activated, &petApp,
                     &PetApp::activateFromSecondInstance);

    petApp.run(!parser.isSet(noAutostart));
    return app.exec();
}

#include "main.moc"
