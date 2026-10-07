// 单实例（QLocalServer/QLocalSocket）：重复启动不叠开，改为唤出已有实例的对话窗口
#pragma once

#include <QObject>
#include <QLocalServer>
#include <QString>

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);
    ~SingleInstance() override;

    /// true = 本进程是唯一实例；false = 已有实例在跑（应随即 notifyExisting 后退出）
    bool tryLock();
    /// 作为第二实例：通知老实例「把对话窗口呼出/切换」
    void notifyExisting();

signals:
    void activated();

private:
    QString m_key;
    QLocalServer m_server;
};
