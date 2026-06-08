#include "agents/AgentProtocol.h"

#include "nats/INatsConnection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace {
// Parse a max_payload string ("512KB", "1MB", "4MB", "1048576") to bytes (§2.1).
// 1024-based units, matching NATS server conventions.
int parsePayloadSize(const QString &raw)
{
    QString s = raw.trimmed().toUpper();
    qint64 mult = 1;
    if (s.endsWith(QLatin1String("KB"))) { mult = 1024; s.chop(2); }
    else if (s.endsWith(QLatin1String("MB"))) { mult = 1024LL * 1024; s.chop(2); }
    else if (s.endsWith(QLatin1String("GB"))) { mult = 1024LL * 1024 * 1024; s.chop(2); }
    else if (s.endsWith(QLatin1String("B"))) { mult = 1; s.chop(1); }
    bool ok = false;
    const qint64 n = s.trimmed().toLongLong(&ok) * mult;
    if (!ok)
        return 0;
    return n > 2'000'000'000LL ? 2'000'000'000 : static_cast<int>(n);
}
}  // namespace

AgentProtocol::AgentProtocol(INatsConnection *conn, QObject *parent)
    : QObject(parent), m_conn(conn)
{
    qRegisterMetaType<QVector<AgentProtocol::DiscoveredAgent>>();
    connect(m_conn, &INatsConnection::messageReceived, this, &AgentProtocol::onMessage);

    // Discovery window: when it fires, stop collecting and publish the batch.
    m_discoveryTimer = new QTimer(this);
    m_discoveryTimer->setSingleShot(true);
    connect(m_discoveryTimer, &QTimer::timeout, this, [this]() {
        if (m_discoverySid) {
            m_conn->unsubscribe(m_discoverySid);
            m_discoverySid = 0;
        }
        emit agentsDiscovered(m_discoveryBatch);
    });
}

void AgentProtocol::startHeartbeatWatch()
{
    if (!m_conn->isConnected())
        return;
    // Fresh subscription each call; after a reconnect the old sid is already gone
    // server-side, so overwriting it just drops a stale local number.
    m_heartbeatSid = m_conn->subscribe(QStringLiteral("agents.hb.*.*.*"));
}

void AgentProtocol::discover(int windowMs)
{
    if (!m_conn->isConnected())
        return;
    if (m_discoverySid)
        m_conn->unsubscribe(m_discoverySid);
    m_discoveryBatch.clear();
    const QString inbox = m_conn->newInbox();
    m_discoverySid = m_conn->subscribe(inbox);
    m_conn->publish(QStringLiteral("$SRV.INFO.agents"), QByteArray(), inbox);
    m_discoveryTimer->start(windowMs);
}

void AgentProtocol::handleDiscoveryReply(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("name")).toString() != QLatin1String("agents"))
        return;   // some other micro service replied to $SRV — ignore

    const QJsonObject md = o.value(QStringLiteral("metadata")).toObject();
    DiscoveredAgent a;
    a.instanceId = o.value(QStringLiteral("id")).toString();
    a.agent = md.value(QStringLiteral("agent")).toString();
    a.owner = md.value(QStringLiteral("owner")).toString();
    a.session = md.value(QStringLiteral("session")).toString();
    a.protocolVersion = md.value(QStringLiteral("protocol_version")).toString();
    a.description = o.value(QStringLiteral("description")).toString();

    for (const QJsonValue &ev : o.value(QStringLiteral("endpoints")).toArray()) {
        const QJsonObject ep = ev.toObject();
        if (ep.value(QStringLiteral("name")).toString() != QLatin1String("prompt"))
            continue;
        a.subject = ep.value(QStringLiteral("subject")).toString();
        const QJsonObject epMeta = ep.value(QStringLiteral("metadata")).toObject();
        // Endpoint metadata values are strings on the wire (e.g. "true"), but
        // tolerate a real bool too.
        const QJsonValue ao = epMeta.value(QStringLiteral("attachments_ok"));
        a.attachmentsOk = ao.isBool()
                              ? ao.toBool()
                              : ao.toString().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        a.maxPayloadBytes = parsePayloadSize(epMeta.value(QStringLiteral("max_payload")).toString());
        break;
    }
    if (a.subject.isEmpty())
        return;   // no prompt endpoint → not addressable

    // Instance name lives only in the subject (§2): 5th token; else the session.
    const QStringList t = a.subject.split(QLatin1Char('.'));
    a.name = (t.size() >= 5) ? t.at(4) : a.session;

    for (const DiscoveredAgent &existing : m_discoveryBatch)
        if (existing.instanceId == a.instanceId)
            return;   // already have this instance
    m_discoveryBatch.append(a);
}

void AgentProtocol::handleHeartbeat(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
        return;
    const QJsonObject o = doc.object();
    const QString id = o.value(QStringLiteral("instance_id")).toString();
    if (id.isEmpty())
        return;
    emit heartbeat(id, o.value(QStringLiteral("interval_s")).toInt(30));
}

QString AgentProtocol::sendPrompt(const QString &subject, const QString &text,
                                  const QList<Attachment> &attachments)
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

    // JSON envelope (§5.1) with optional attachments (§5.2): standard padded base64.
    QJsonObject envelope{{QStringLiteral("prompt"), text}};
    if (!attachments.isEmpty()) {
        QJsonArray arr;
        for (const Attachment &a : attachments) {
            arr.append(QJsonObject{
                {QStringLiteral("filename"), a.filename},
                {QStringLiteral("content"), QString::fromLatin1(a.content.toBase64())},
            });
        }
        envelope.insert(QStringLiteral("attachments"), arr);
    }
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
    // Route by subscription: heartbeats, the discovery inbox, then prompt inboxes.
    if (m_heartbeatSid != 0 && sid == m_heartbeatSid) {
        handleHeartbeat(payload);
        return;
    }
    if (m_discoverySid != 0 && sid == m_discoverySid) {
        handleDiscoveryReply(payload);
        return;
    }

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
