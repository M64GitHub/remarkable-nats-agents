#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Transport seam. Everything above the wire (AgentProtocol, the models, QML)
// depends only on this interface, never on QTcpSocket, so the concrete client
// can later be replaced (e.g. nats.zig) without touching the protocol layer.
//
// This is intentionally a thin core-NATS surface — connect, pub, sub, unsub,
// plus an inbox factory for request/reply. The Synadia Agent Protocol (envelope,
// streaming chunks, $SRV discovery, heartbeats) lives one layer up in
// AgentProtocol; this layer knows nothing about it.
class INatsConnection : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~INatsConnection() override = default;

    virtual void connectToServer(const QString &host, quint16 port) = 0;
    virtual void disconnectFromServer() = 0;
    virtual bool isConnected() const = 0;

    // SUB. Returns the subscription id (sid) used to correlate inbound messages
    // and to UNSUB later. `subject` may contain NATS wildcards.
    virtual quint64 subscribe(const QString &subject, const QString &queue = QString()) = 0;
    virtual void unsubscribe(quint64 sid, int maxMsgs = 0) = 0;

    // PUB. `replyTo` is the reply subject for request/reply (empty for fire-and-forget).
    virtual void publish(const QString &subject, const QByteArray &payload,
                         const QString &replyTo = QString()) = 0;

    // A fresh unique reply subject, e.g. "_INBOX.<token>.<n>", for request/reply.
    virtual QString newInbox() = 0;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);

    // One delivered NATS message.
    //
    // `headers` is empty when the message carried no NATS header block (a plain
    // MSG). Per core-protocol.md §6.5 the stream terminator is precisely a
    // zero-byte `payload` with empty `headers`. Service errors (§9) arrive as
    // header fields "Nats-Service-Error-Code"/"Nats-Service-Error"; an inline
    // status line (e.g. a 503 no-responders) is surfaced as headers["Status-Code"].
    void messageReceived(quint64 sid, const QString &subject, const QString &reply,
                         const QByteArray &payload, const QVariantMap &headers);
};
