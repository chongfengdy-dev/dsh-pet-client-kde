// 系统托盘状态灯（KDE StatusNotifierItem）
//
// 2026-10-06 主决定：移除桌面悬浮鲸鱼窗口——它的功能（点一下呼出对话、显示状态）
// 与托盘完全重复，还占桌面。状态显示改由托盘图标独立承担：
//   蓝=空闲 / 黑=对话窗口收起 / 橙=提问·待授权（400ms 闪烁）/ 绿=回复完成
#pragma once

#include "petcolor.h"

#include <QIcon>
#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QTimer;

class Tray : public QObject
{
    Q_OBJECT

public:
    explicit Tray(QObject *parent = nullptr);

    /// 基色：对话窗口可见=蓝 / 收起或未打开=黑
    void setBaseColor(PetColor c);
    /// 服务端状态：橙=提问·待授权、绿=回复完成、蓝/黑=跟随基色
    void setRemoteColor(PetColor c);

    /// 当前实际显示色（含闪烁相位）
    PetColor displayColor() const;

    /// 弹一条气泡通知（切换模式等操作的反馈）
    void showMessage(const QString &title, const QString &body);

signals:
    void activateRequested();                        ///< 左键单击托盘图标
    void modeSwitchRequested(const QString &mode);   ///< "full" / "clean"
    void quitRequested();

private:
    void rebuildMenu();
    void loadAssets();
    void refreshIcon();
    void updateBlinkTimer();

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_fullAction = nullptr;
    QAction *m_cleanAction = nullptr;

    QIcon m_icons[4];
    PetColor m_base = PetColor::Blue;
    PetColor m_remote = PetColor::Blue;
    bool m_blinkOn = true;
    QTimer *m_blinkTimer = nullptr;
};
