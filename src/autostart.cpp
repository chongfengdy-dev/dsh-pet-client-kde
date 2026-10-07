#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString configHome()
{
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty())
        return xdg;
    return QDir::homePath() + QStringLiteral("/.config");
}

} // namespace

QString Autostart::desktopFilePath(const QString &appName)
{
    return configHome() + QStringLiteral("/autostart/") + appName + QStringLiteral(".desktop");
}

bool Autostart::isInstalled(const QString &appName)
{
    return QFile::exists(desktopFilePath(appName));
}

bool Autostart::install(const QString &appName, const QString &displayName, const QString &execPath)
{
    const QString path = desktopFilePath(appName);
    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString exe = execPath.isEmpty() ? QCoreApplication::applicationFilePath() : execPath;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("dsh-pet: 无法写入自启项 %s", qPrintable(path));
        return false;
    }
    QTextStream out(&f);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=" << displayName << "\n"
        << "Comment=DeepSeek Harness 托盘\n"
        << "Exec=" << exe << "\n"
        << "Icon=dsh-pet\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n"
        << "StartupNotify=false\n";
    f.close();
    return true;
}

bool Autostart::remove(const QString &appName)
{
    const QString path = desktopFilePath(appName);
    if (!QFile::exists(path))
        return true;
    return QFile::remove(path);
}
