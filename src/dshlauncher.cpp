#include "dshlauncher.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {

const char *kWebUrl = "http://127.0.0.1:3080";
const char *kTokenFile = "dsh-web-token.txt";

/// 脚本是否走 systemd user 单元（= Linux 原生版，无需 sudo，可直接执行）
bool looksNative(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.read(8192).contains(QByteArrayLiteral("systemctl --user"));
}

/// 按顺序取第一个存在的文件（优先有执行位的）
QString firstExisting(const QStringList &candidates)
{
    for (const QString &c : candidates) {
        const QFileInfo fi(c);
        if (fi.exists() && fi.isFile() && fi.isExecutable())
            return fi.canonicalFilePath();
    }
    for (const QString &c : candidates) {   // 退一步：没有执行位也可用 bash 跑
        const QFileInfo fi(c);
        if (fi.exists() && fi.isFile())
            return fi.canonicalFilePath();
    }
    return {};
}

QString readTokenFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString raw = QString::fromUtf8(f.readAll()).trimmed();
    if (raw.isEmpty())
        return {};
    const int i = raw.indexOf(QStringLiteral("?token="));
    const QString token = (i >= 0) ? raw.mid(i + 7) : raw;
    return token.section(QLatin1Char('&'), 0, 0);   // 只保留 token 本身
}

QStringList tokenFileCandidates()
{
    QStringList out;
    if (const QString env = qEnvironmentVariable("DSH_WEB_TOKEN_FILE"); !env.isEmpty())
        out << env;
    out << QCoreApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(kTokenFile);
    out << QDir::homePath() + QStringLiteral("/.dsh/") + QLatin1String(kTokenFile);
    if (const QString root = qEnvironmentVariable("DSH_ROOT"); !root.isEmpty())
        out << root + QLatin1String("/nim-client/") + QLatin1String(kTokenFile);
    return out;
}

/// 读 .desktop 的 Name= 字段
QString desktopName(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1String("Name=")))
            return line.mid(5);
        if (line.startsWith(QLatin1Char('[')) && line != QLatin1String("[Desktop Entry]"))
            break;
    }
    return {};
}

} // namespace

QString DshLauncher::bootUrl()
{
    const QStringList candidates = tokenFileCandidates();
    for (const QString &path : candidates) {
        const QString token = readTokenFrom(path);
        if (!token.isEmpty())
            return QLatin1String(kWebUrl) + QStringLiteral("?token=") + token;
    }
    return QLatin1String(kWebUrl);
}

QString DshLauncher::findPwaDesktopFile()
{
    // PWA 安装后浏览器会在用户级 applications 目录生成 .desktop；
    // 目录名与文件名随浏览器而异（Chrome/Cent/Edge/Brave…），故按内容匹配、不硬编码
    QStringList roots;
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    roots << (dataHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share")
                                 : dataHome) + QStringLiteral("/applications");
    roots << QStringLiteral("/usr/share/applications");
    roots << QStringLiteral("/usr/local/share/applications");

    for (const QString &root : roots) {
        QDirIterator it(root, {QStringLiteral("*.desktop")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (desktopName(path).contains(QLatin1String("deepseek harness"),
                                           Qt::CaseInsensitive))
                return path;
        }
    }
    return {};
}

bool DshLauncher::openDsh(QString *howLaunched)
{
    const QString pwa = findPwaDesktopFile();
    if (!pwa.isEmpty()) {
        if (QProcess::startDetached(QStringLiteral("gio"),
                                    {QStringLiteral("launch"), pwa})) {
            if (howLaunched)
                *howLaunched = QStringLiteral("PWA: ") + pwa;
            return true;
        }
    }
    const QString url = bootUrl();
    const bool ok = QDesktopServices::openUrl(QUrl(url));
    if (howLaunched)
        *howLaunched = ok ? QStringLiteral("浏览器: ") + url : QStringLiteral("打开失败");
    return ok;
}

DshLauncher::ModeScriptInfo DshLauncher::findModeScript()
{
    QStringList nativeCandidates;   // Linux 原生版（systemctl --user）
    QStringList legacyCandidates;   // WSL 版（sudo systemctl，只能给用户复制指令）

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString dataHome = QDir::homePath();

    if (const QString env = qEnvironmentVariable("DSH_MODE_SCRIPT"); !env.isEmpty())
        nativeCandidates << env;

    nativeCandidates << exeDir + QStringLiteral("/dsh-mode-linux.sh");
    nativeCandidates << exeDir + QStringLiteral("/../scripts/dsh-mode-linux.sh");
    nativeCandidates << exeDir + QStringLiteral("/../share/dsh-pet/dsh-mode-linux.sh");
    nativeCandidates << dataHome + QStringLiteral("/.dsh/scripts/dsh-mode-linux.sh");

    legacyCandidates << exeDir + QStringLiteral("/../nim-client/dsh-mode.sh");
    legacyCandidates << exeDir + QStringLiteral("/../../nim-client/dsh-mode.sh");
    if (const QString root = qEnvironmentVariable("DSH_ROOT"); !root.isEmpty())
        legacyCandidates << root + QStringLiteral("/nim-client/dsh-mode.sh");
    legacyCandidates << dataHome + QStringLiteral("/.dsh/scripts/dsh-mode.sh");

    ModeScriptInfo info;
    if (const QString p = firstExisting(nativeCandidates); !p.isEmpty()) {
        info.path = p;
        info.native = true;
        return info;
    }
    if (const QString p = firstExisting(legacyCandidates); !p.isEmpty()) {
        info.path = p;
        info.native = looksNative(p);   // 老文件名但内容已是 user 单元版 → 也可直接执行
        return info;
    }
    return info;
}

QString DshLauncher::modeSwitchCommand(const QString &mode)
{
    const ModeScriptInfo info = findModeScript();
    if (info.path.isEmpty())
        return {};
    const QString cmd = QStringLiteral("bash \"%1\" %2").arg(info.path, mode);
    return info.native ? cmd : QStringLiteral("sudo ") + cmd;
}

QStringList DshLauncher::serviceUnits()
{
    // 动态枚举而不是写死服务名：以后新增 dsh-xxx 服务会自动纳入。
    // 托盘客户端自身若也注册成 dsh-* 单元，必须排除——停了它就没代码再拉起来了。
    QProcess p;
    p.start(QStringLiteral("systemctl"),
            {QStringLiteral("--user"), QStringLiteral("list-unit-files"),
             QStringLiteral("dsh-*.service"), QStringLiteral("--no-legend"),
             QStringLiteral("--no-pager")});
    if (!p.waitForFinished(4000))
        return {};

    QStringList units;
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString unit = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                        Qt::SkipEmptyParts)
                                 .value(0);
        if (!unit.endsWith(QLatin1String(".service")))
            continue;
        if (unit.contains(QLatin1String("pet"), Qt::CaseInsensitive))
            continue;
        units << unit;
    }
    units.removeDuplicates();
    return units;
}

bool DshLauncher::servicesActive()
{
    const QStringList units = serviceUnits();
    if (units.isEmpty())
        return false;

    QProcess p;
    QStringList args{QStringLiteral("--user"), QStringLiteral("is-active")};
    args << units;
    p.start(QStringLiteral("systemctl"), args);
    if (!p.waitForFinished(4000))
        return false;

    const QStringList states = QString::fromUtf8(p.readAllStandardOutput())
                                   .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &s : states) {
        if (s.trimmed() == QLatin1String("active"))
            return true;
    }
    return false;
}
