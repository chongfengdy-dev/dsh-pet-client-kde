// 宠物状态来源：优先 HTTP（127.0.0.1:3081 /api/pet-state），失败回落本地文件。
// Windows 版之所以只用文件，是 Nim 的 net 模块会在主循环里崩 0xc0000005；
// C++/Qt 没有这个限制，且 Linux 侧文件回执路径是 Windows 专用的，
// 因此本版以 HTTP 为主通道（与 qt-client-env.md 的建议一致）。
#pragma once

#include "petcolor.h"

#include <QObject>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

class PetState : public QObject
{
    Q_OBJECT

public:
    explicit PetState(QObject *parent = nullptr);

    void start();
    void setPort(int port) { m_port = port; }

    PetColor color() const { return m_color; }
    qint64 doneAt() const { return m_doneAt; }
    bool lastPollOk() const { return m_pollOk; }

    /// 回执：用户已看到回复 → 服务端把 green 转回 blue
    void ack();

signals:
    void updated(PetColor color, qint64 doneAt);

private:
    void scheduleNext();
    void poll();
    void handleStateReply(QNetworkReply *reply, bool fromNetwork);
    void parseState(const QByteArray &body, bool *ok, PetColor *color, qint64 *doneAt);
    void readFallbackFile(bool *ok, PetColor *color, qint64 *doneAt);

    QNetworkAccessManager *m_net = nullptr;
    QTimer m_timer;
    int m_port = 3081;
    PetColor m_color = PetColor::Blue;
    qint64 m_doneAt = 0;
    bool m_pollOk = false;
    bool m_inFlight = false;
};
