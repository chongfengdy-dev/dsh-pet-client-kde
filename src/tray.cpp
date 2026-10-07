#include "tray.h"
#include "dshlauncher.h"

#include <QAction>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>

namespace {

constexpr int kFishDraw = 80;                              // fish_*.bin 的像素尺寸
constexpr int kBlinkMs = 400;                              // 橙：心跳闪烁相位切换间隔
constexpr int kIconSizes[] = {16, 22, 24, 32, 48, 64};     // 让托盘/菜单各取所需

QString fishPath(PetColor c)
{
    switch (c) {
    case PetColor::Black:
        return QStringLiteral(":/assets/fish_black.bin");
    case PetColor::Orange:
        return QStringLiteral(":/assets/fish_orange.bin");
    case PetColor::Green:
        return QStringLiteral(":/assets/fish_green.bin");
    case PetColor::Blue:
        break;
    }
    return QStringLiteral(":/assets/fish_blue.bin");
}

QImage loadBin(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray raw = f.readAll();
    if (raw.size() != kFishDraw * kFishDraw * 4)
        return {};
    // bin 为 BGRA 预乘小端；x86 上即 ARGB32_Premultiplied 的内存布局
    const QImage view(reinterpret_cast<const uchar *>(raw.constData()),
                      kFishDraw, kFishDraw, kFishDraw * 4,
                      QImage::Format_ARGB32_Premultiplied);
    return view.copy();
}

QIcon makeIcon(const QImage &src)
{
    QIcon icon;
    if (src.isNull())
        return icon;
    for (int s : kIconSizes)
        icon.addPixmap(QPixmap::fromImage(
            src.scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    return icon;
}

} // namespace

Tray::Tray(QObject *parent)
    : QObject(parent)
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(QStringLiteral("DeepSeek Harness"));

    loadAssets();
    rebuildMenu();

    // 顺序很重要：先给图标再 show()，否则启动瞬间会报
    // "QSystemTrayIcon::setVisible: No Icon set"（2026-10-06 实测发现）
    refreshIcon();
    m_tray->show();

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setTimerType(Qt::PreciseTimer);
    connect(m_blinkTimer, &QTimer::timeout, this, [this] {
        m_blinkOn = !m_blinkOn;
        refreshIcon();
    });

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    emit activateRequested();
            });
}

void Tray::loadAssets()
{
    for (int i = 0; i < 4; ++i) {
        const QImage img = loadBin(fishPath(PetColor(i)));
        m_icons[i] = makeIcon(img);
        if (m_icons[i].isNull())
            qWarning("dsh-pet: 托盘图标素材缺失: %s", qPrintable(fishPath(PetColor(i))));
    }
}

void Tray::rebuildMenu()
{
    if (m_menu)
        m_menu->deleteLater();
    m_menu = new QMenu();

    // 服务开关放最上面：打游戏前一键腾内存，是这里最常用的操作
    m_svcStartAction = m_menu->addAction(QStringLiteral("启动 dsh 服务"));
    connect(m_svcStartAction, &QAction::triggered, this,
            [this] { emit dshServicesStartRequested(); });

    m_svcStopAction = m_menu->addAction(QStringLiteral("退出 dsh 服务（全部）"));
    connect(m_svcStopAction, &QAction::triggered, this,
            [this] { emit dshServicesStopRequested(); });

    // 每次展开菜单时按真实运行状态决定哪一项可用（开关语义）
    connect(m_menu, &QMenu::aboutToShow, this, [this] {
        const bool active = DshLauncher::servicesActive();
        if (m_svcStartAction)
            m_svcStartAction->setEnabled(!active);
        if (m_svcStopAction)
            m_svcStopAction->setEnabled(active);
    });

    m_menu->addSeparator();

    m_fullAction = m_menu->addAction(QStringLiteral("切换：完整模式（复制指令）"));
    connect(m_fullAction, &QAction::triggered, this,
            [this] { emit modeSwitchRequested(QStringLiteral("full")); });

    m_cleanAction = m_menu->addAction(QStringLiteral("切换：纯净模式（复制指令）"));
    connect(m_cleanAction, &QAction::triggered, this,
            [this] { emit modeSwitchRequested(QStringLiteral("clean")); });

    m_menu->addSeparator();

    QAction *quit = m_menu->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, this, [this] { emit quitRequested(); });

    m_tray->setContextMenu(m_menu);
}

PetColor Tray::displayColor() const
{
    // 橙=心跳闪烁（相位 OFF 时回到基色）；绿=常亮；蓝/黑=跟随对话窗口是否可见
    if (m_remote == PetColor::Orange)
        return m_blinkOn ? PetColor::Orange : m_base;
    if (m_remote == PetColor::Green)
        return PetColor::Green;
    return m_base;
}

void Tray::refreshIcon()
{
    const QIcon &icon = m_icons[petColorIndex(displayColor())];
    if (!icon.isNull())
        m_tray->setIcon(icon);
}

void Tray::updateBlinkTimer()
{
    const bool shouldBlink = (m_remote == PetColor::Orange);
    if (shouldBlink && !m_blinkTimer->isActive()) {
        m_blinkOn = true;
        m_blinkTimer->start(kBlinkMs);
    } else if (!shouldBlink && m_blinkTimer->isActive()) {
        m_blinkTimer->stop();
        m_blinkOn = true;   // 复位，免得下次进橙色时从基色相位开始
    }
}

void Tray::setBaseColor(PetColor c)
{
    if (m_base == c)
        return;
    m_base = c;
    refreshIcon();
}

void Tray::setRemoteColor(PetColor c)
{
    if (m_remote == c)
        return;
    m_remote = c;
    updateBlinkTimer();
    refreshIcon();
}

void Tray::showMessage(const QString &title, const QString &body)
{
    if (m_tray)
        m_tray->showMessage(title, body, QSystemTrayIcon::Information, 4000);
}
