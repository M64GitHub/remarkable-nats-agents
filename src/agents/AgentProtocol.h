#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

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
    explicit AgentProtocol(INatsConnection *conn, QObject *parent = nullptr);

    INatsConnection *connection() const { return m_conn; }

    // Send `text` to a prompt endpoint `subject` (learned from discovery or, in
    // M1, from static config). Returns an opaque request id that tags every
    // signal emitted for this stream. Empty text is rejected (returns "").
    QString sendPrompt(const QString &subject, const QString &text);

signals:
    void promptAck(const QString &requestId);                                  // {type:"status",data:"ack"}
    void promptResponse(const QString &requestId, const QString &textDelta);   // {type:"response",...}
    void promptComplete(const QString &requestId);                             // headerless terminator
    void promptError(const QString &requestId, int code, const QString &message);

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

    INatsConnection *m_conn = nullptr;
    QHash<quint64, Request> m_bySid;   // sid -> in-flight request
    int m_inactivityMs = 60000;        // §6.6 recommended default
};
