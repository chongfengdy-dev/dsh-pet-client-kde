// KWin 桥：把「客户端无权做但合成器能做」的事交给 KWin 脚本。
//
// 纯 Wayland 下：
//   · 窗口不能自己决定位置，也拿不到全局鼠标坐标
//   · Qt::WindowStaysOnTopHint 不保证置顶
//   · 无法枚举/激活别的应用窗口（wmctrl/xdotool 失效）
// 因此本类负责：
//   1) 启动时把内嵌的 KWin 脚本写入运行时目录，经 D-Bus 注入 KWin
//      （脚本负责：宠物窗口置顶 + 贴右下角；监视 DSH 对话窗口状态）
//   2) 通过「命令脚本重载」请求 KWin 切换 DSH 对话窗口（激活/最小化）
//   3) 注册 session D-Bus 服务 org.dshpet.Pet，接收 KWin 脚本回调
#pragma once

#include <QObject>
#include <QString>

class QDBusInterface;

class KWinBridge : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.dshpet.Pet")

public:
    explicit KWinBridge(QObject *parent = nullptr);
    ~KWinBridge() override;

    /// 注入常驻脚本（非 KWin 会话下静默降级）
    void install();
    bool available() const { return m_available; }

    /// 点鲸鱼/托盘左键：对话窗口存在则切换（收起↔呼出），不存在则交给上层拉起
    void toggleDshWindow();
    /// 明确激活对话窗口（不切换）
    void activateDshWindow();
    /// 关闭对话窗口（退出客户端时使用，对应 Windows 版的 WM_CLOSE）
    void closeDshWindow();

    /// 设置任务栏提醒模式（由 PetApp 在状态变化时调用，脚本按需查询）
    void setAttentionMode(const QString &mode);

    /// 让 DSH 窗口请求/清除任务栏注意（KDE demandsAttention）
    void setAttention(bool on);

signals:
    /// 由 KWin 脚本回调：对话窗口是否存在 / 是否最小化 / 是否处于前台
    void dshWindowState(bool exists, bool minimized, bool active);
    /// 请求切换但对话窗口不存在（上层负责拉起 PWA/浏览器）
    void dshWindowMissing();

public slots:
    // ---- KWin 脚本回调入口（D-Bus）----
    void DshWindowState(bool exists, bool minimized, bool active);

    /// KWin 常驻脚本每 400ms 来查一次「任务栏提醒模式」：
    ///   "off" 不提醒 / "blink" 闪烁（等待决策）/ "steady" 常亮（回复完成）
    QString AttentionMode() const;

private:
    void writeScript(const QString &path, const QString &body);
    bool loadScript(const QString &path, const QString &pluginName);
    void unloadScript(const QString &pluginName);
    void runCommand(const QString &command);

    QString m_dir;
    QString m_kwinScriptPath;
    QString m_cmdScriptPath;
    bool m_busOk = false;
    bool m_available = false;
    bool m_dshExists = false;
    bool m_dshMinimized = true;
    QString m_attentionMode = QStringLiteral("off");   // 供 KWin 常驻脚本查询
};
