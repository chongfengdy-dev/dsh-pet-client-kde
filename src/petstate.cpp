#include "petstate.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr int kPollOkMs = 1000;      // 正常 1s（Windows 版同频）
constexpr int kPollFailMs = 5000;    // 失败拉长到 5s，少打扰
}

PetState::PetState(QObject *parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &PetState::poll);
}

void PetState::start()
{
    poll();
}

void PetState::scheduleNext()
{
    m_timer.start(m_pollOk ? kPollOkMs : kPollFailMs);
}

void PetState::poll()
{
    if (m_inFlight)
        return;
    m_inFlight = true;

    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/api/pet-state").arg(m_port)));
    req.setTransferTimeout(2500);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_inFlight = false;
        const QByteArray body = reply->readAll();
        const bool netOk = (reply->error() == QNetworkReply::NoError) && !body.isEmpty();
        reply->deleteLater();

        bool ok = false;
        PetColor color = m_color;
        qint64 doneAt = m_doneAt;
        if (netOk)
            parseState(body, &ok, &color, &doneAt);
        if (!ok)
            readFallbackFile(&ok, &color, &doneAt);   // HTTP 不可用 → 文件回退

        m_pollOk = ok;
        if (ok && (color != m_color || doneAt != m_doneAt)) {
            m_color = color;
            m_doneAt = doneAt;
            emit updated(m_color, m_doneAt);
        } else if (ok) {
            m_color = color;
            m_doneAt = doneAt;
        }
        scheduleNext();
    });
}

void PetState::parseState(const QByteArray &body, bool *ok, PetColor *color, qint64 *doneAt)
{
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    const QString pet = obj.value(QStringLiteral("pet")).toString();
    if (pet == QLatin1String("blue"))
        *color = PetColor::Blue;
    else if (pet == QLatin1String("black"))
        *color = PetColor::Black;
    else if (pet == QLatin1String("orange"))
        *color = PetColor::Orange;
    else if (pet == QLatin1String("green"))
        *color = PetColor::Green;
    else
        return;
    *doneAt = qint64(obj.value(QStringLiteral("doneAt")).toDouble(0));
    *ok = true;
}

void PetState::readFallbackFile(bool *ok, PetColor *color, qint64 *doneAt)
{
    // 路径动态化：家目录下的 pet-state.json（服务端只写一个固定位置）
    QFile f(QDir::homePath() + QStringLiteral("/pet-state.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    parseState(f.readAll(), ok, color, doneAt);
}

void PetState::ack()
{
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/api/pet-ack").arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(2500);
    QNetworkReply *reply = m_net->post(req, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}
