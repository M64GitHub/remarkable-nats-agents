#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

class INatsConnection;
class QTimer;

// Synadia Agent Protocol (core-protocol.md v0.3) on top of a NATS transport.
//
// M1 implements the request/streaming-response half: send a prompt to a known
// endpoint subject and surface the reply stream. A reply stream is a sequence of
// typed JSON chunks on a reply inbox — an `ack` status first, then one or more
// `response` chunks — terminated by a zero-byte, header-less message (§6).
// Errors arrive as a message carrying Nats-Service-Error-Code headers (§9)
// before the terminator. A per-stream inactivity timeout (§6.6) guards against a
// lost terminator.
//
// $SRV discovery (§4) and heartbeat liveness (§8) are M2 and layer onto the same
// transport; the request subject in M1 comes from static roster config instead.
class AgentProtocol : public QObject
{
    Q_OBJECT
public:
    // One agent instance as reported by $SRV.INFO.agents (§4.3, §B.12).
    struct DiscoveredAgent {
        QString instanceId;       // service `id` == heartbeat instance_id
        QString agent;            // metadata.agent
        QString owner;            // metadata.owner
        QString session;          // metadata.session (may be empty)
        QString name;             // 5th token of the prompt subject (else session)
        QString description;
        QString subject;          // prompt endpoint subject — publish here
        QString protocolVersion;  // metadata.protocol_version
        bool attachmentsOk = false;
    };

    explicit AgentProtocol(INatsConnection *conn, QObject *parent = nullptr);

    INatsConnection *connection() const { return m_conn; }

    // Send `text` to a prompt endpoint `subject` (learned from discovery, or from
    // static config). Returns an opaque request id that tags every signal emitted
    // for this stream. Empty text is rejected (returns "").
    QString sendPrompt(const QString &subject, const QString &text);

    // Subscribe to the heartbeat wildcard (agents.hb.*.*.*). Call before discover()
    // so we don't miss a just-discovered agent's first beat (§8.5). Idempotent-ish:
    // safe to call again after a reconnect (re-subscribes).
    void startHeartbeatWatch();

    // Scatter-gather $SRV.INFO.agents (§4.1): collect every instance's reply for
    // `windowMs`, then emit agentsDiscovered() once with the batch.
    void discover(int windowMs = 2000);

signals:
    void promptAck(const QString &requestId);                                  // {type:"status",data:"ack"}
    void promptResponse(const QString &requestId, const QString &textDelta);   // {type:"response",...}
    void promptComplete(const QString &requestId);                             // headerless terminator
    void promptError(const QString &requestId, int code, const QString &message);

    void agentsDiscovered(const QVector<AgentProtocol::DiscoveredAgent> &agents);
    void heartbeat(const QString &instanceId, int intervalS);                  // liveness beacon (§8)

private slots:
    void onMessage(quint64 sid, const QString &subject, const QString &reply,
                   const QByteArray &payload, const QVariantMap &headers);

private:
    struct Request {
        QString id;
        quint64 sid = 0;
        QString inbox;
        QTimer *timer = nullptr;
        bool errored = false;
    };
    void resetInactivity(Request &req);
    void finish(quint64 sid);   // unsubscribe + drop bookkeeping
    void handleDiscoveryReply(const QByteArray &payload);
    void handleHeartbeat(const QByteArray &payload);

    INatsConnection *m_conn = nullptr;
    QHash<quint64, Request> m_bySid;   // sid -> in-flight prompt request
    int m_inactivityMs = 60000;        // §6.6 recommended default

    quint64 m_heartbeatSid = 0;        // subscription for agents.hb.*.*.*
    quint64 m_discoverySid = 0;        // in-flight $SRV.INFO reply inbox (0 = none)
    QTimer *m_discoveryTimer = nullptr;
    QVector<DiscoveredAgent> m_discoveryBatch;
};

Q_DECLARE_METATYPE(AgentProtocol::DiscoveredAgent)
