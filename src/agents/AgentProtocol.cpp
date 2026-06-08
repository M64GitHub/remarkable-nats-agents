#include "agents/AgentProtocol.h"

#include "nats/INatsConnection.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QUuid>

AgentProtocol::AgentProtocol(INatsConnection *conn, QObject *parent)
    : QObject(parent), m_conn(conn)
{
    connect(m_conn, &INatsConnection::messageReceived, this, &AgentProtocol::onMessage);
}

QString AgentProtocol::sendPrompt(const QString &subject, const QString &text)
{
    if (text.trimmed().isEmpty() || subject.isEmpty())
        return QString();

    Request req;
    req.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    req.inbox = m_conn->newInbox();
    req.sid = m_conn->subscribe(req.inbox);

    req.timer = new QTimer(this);
    req.timer->setSingleShot(true);
    const quint64 sid = req.sid;
    connect(req.timer, &QTimer::timeout, this, [this, sid]() {
        auto it = m_bySid.find(sid);
        if (it == m_bySid.end())
            return;
        const QString id = it->id;
        finish(sid);
        emit promptError(id, -1, QStringLiteral("stream inactivity timeout"));
    });

    m_bySid.insert(req.sid, req);

    // Send the JSON envelope form (§5.1). Plain text would also be valid, but
    // the explicit envelope is the shape we extend with attachments later.
    QJsonObject envelope{{QStringLiteral("prompt"), text}};
    const QByteArray payload = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    m_conn->publish(subject, payload, req.inbox);

    resetInactivity(m_bySid[req.sid]);
    return req.id;
}

void AgentProtocol::resetInactivity(Request &req)
{
    if (req.timer)
        req.timer->start(m_inactivityMs);
}

void AgentProtocol::finish(quint64 sid)
{
    auto it = m_bySid.find(sid);
    if (it == m_bySid.end())
        return;
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    m_conn->unsubscribe(sid);
    m_bySid.erase(it);
}

void AgentProtocol::onMessage(quint64 sid, const QString &, const QString &,
                              const QByteArray &payload, const QVariantMap &headers)
{
    auto it = m_bySid.find(sid);
    if (it == m_bySid.end())
        return;   // not one of our reply inboxes
    Request &req = it.value();
    resetInactivity(req);

    // Service error (§9): headers carry Nats-Service-Error-Code. The error
    // message precedes the terminator; we report it now and finalise when the
    // terminator (or timeout) arrives.
    if (headers.contains(QStringLiteral("Nats-Service-Error-Code"))) {
        const int code = headers.value(QStringLiteral("Nats-Service-Error-Code")).toInt();
        QString message = headers.value(QStringLiteral("Nats-Service-Error")).toString();
        const QJsonDocument body = QJsonDocument::fromJson(payload);
        if (body.isObject() && body.object().contains(QStringLiteral("message")))
            message = body.object().value(QStringLiteral("message")).toString();
        req.errored = true;
        emit promptError(req.id, code, message);
        return;
    }

    // Inline status >= 400 (e.g. 503 no-responders: the prompt subject had no
    // subscriber). Treat as a transport-level error and finalise.
    const int statusCode = headers.value(QStringLiteral("Status-Code")).toInt();
    if (statusCode >= 400) {
        const QString id = req.id;
        finish(sid);
        emit promptError(id, statusCode,
                         statusCode == 503 ? QStringLiteral("no agent is listening on that subject")
                                           : QStringLiteral("transport error %1").arg(statusCode));
        return;
    }

    // Terminator (§6.5): zero-byte body, no headers.
    if (payload.isEmpty() && headers.isEmpty()) {
        const QString id = req.id;
        const bool errored = req.errored;
        finish(sid);
        if (!errored)
            emit promptComplete(id);
        return;
    }

    // Otherwise a typed chunk: {"type": "...", "data": ...} (§6.2).
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return;   // §6.2: non-terminating chunks must be JSON objects; ignore junk
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    const QJsonValue data = obj.value(QStringLiteral("data"));

    if (type == QLatin1String("status")) {
        if (data.toString() == QLatin1String("ack"))
            emit promptAck(req.id);
        // Unknown status values: silently ignore (§6.4).
    } else if (type == QLatin1String("response")) {
        // `data` is either a bare string or an object with a `text` field (§6.3).
        QString textDelta;
        if (data.isString())
            textDelta = data.toString();
        else if (data.isObject())
            textDelta = data.toObject().value(QStringLiteral("text")).toString();
        if (!textDelta.isEmpty())
            emit promptResponse(req.id, textDelta);
    }
    // type == "query" (§7) and unknown types: ignored in M1 (§6.6 forward-compat).
}
