// 开机自启：~/.config/autostart/*.desktop（XDG 标准，KDE/GNOME 通用）
#pragma once

#include <QString>

namespace Autostart {

QString desktopFilePath(const QString &appName);
bool isInstalled(const QString &appName);
/// 写入/更新自启项；execPath 为空时取当前可执行文件
bool install(const QString &appName, const QString &displayName, const QString &execPath = QString());
bool remove(const QString &appName);

} // namespace Autostart
