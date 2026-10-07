// DSH 对话界面的拉起与定位（对应 Windows 版 findPwaShortcut / ShellExecuteW）
//
// Windows 版靠开始菜单里的 PWA 快捷方式（.lnk）拉起独立应用窗口；Linux 侧等价物是
// 浏览器安装 PWA 时生成的 .desktop 文件（如 chrome-<appid>-Default.desktop）。
// 找不到 PWA 时回落到默认浏览器打开 127.0.0.1:3080（带认证引导 token）。
#pragma once

#include <QString>
#include <QStringList>

namespace DshLauncher {

struct ModeScriptInfo {
    QString path;
    bool native = false;   ///< true = Linux 原生版（systemctl --user，客户端可直接执行，无需 sudo）
};

/// 认证引导 URL：token 文件有效则带 ?token=，否则回落干净 URL
QString bootUrl();
/// 找到的 PWA 桌面项（空 = 未安装 PWA）
QString findPwaDesktopFile();
/// 拉起对话界面：优先 PWA，回落默认浏览器。返回 true 表示已发出启动请求
bool openDsh(QString *howLaunched = nullptr);
/// 定位模式切换脚本（优先 Linux 原生版；WSL 版需要 sudo，只能给用户复制指令）
ModeScriptInfo findModeScript();
/// 需要用户到终端执行的命令文本（含动态路径，不硬编码家目录）
QString modeSwitchCommand(const QString &mode);

/// dsh 后端服务单元：动态枚举用户级 dsh-*.service（排除托盘客户端自身）
QStringList serviceUnits();
/// 是否有任一 dsh 后端服务正在运行（决定菜单里哪一项可用）
bool servicesActive();

} // namespace DshLauncher
