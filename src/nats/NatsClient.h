#pragma once

#include "nats/INatsConnection.h"

#include <QByteArray>
#include <QSslSocket>
#include <QString>

// Minimal NATS client over QSslSocket (QtNetwork — present on host and device).
// Implements the text control protocol: INFO/CONNECT handshake, PING/PONG
// keepalive, PUB/SUB/UNSUB, and inbound MSG + HMSG framing.
//
// Plaintext core NATS and TLS+JWT/NKEY (NGS / Synadia Cloud) both run through the
// one socket: for `tls` we connect, read INFO, upgrade (startClientEncryption),
// then CONNECT with the signed nonce. JetStream is out of scope.
class NatsClient : public INatsConnection
{
    Q_OBJECT
public:
    explicit NatsClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port,
                         bool tls = false, const QString &credsPath = QString()) override;
    void disconnectFromServer() override;
    bool isConnected() const override;

    quint64 subscribe(const QString &subject, const QString &queue = QString()) override;
    void unsubscribe(quint64 sid, int maxMsgs = 0) override;
    void publish(const QString &subject, const QByteArray &payload,
                 const QString &replyTo = QString()) override;
    QString newInbox() override;

private slots:
    void onSocketConnected();
    void onEncrypted();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void write(const QByteArray &bytes);   // queues until handshake completes
    void completeHandshake();               // send CONNECT, flush, emit connected()
    void processBuffer();                   // drive the frame parser
    bool readControlLine(QByteArray &line); // pop one CRLF-terminated control line
    void handleControl(const QByteArray &line);
    void deliverPending();                  // emit once a full MSG/HMSG payload is buffered
    static QVariantMap parseHeaders(const QByteArray &block);

    QSslSocket m_socket;
    QByteArray m_buffer;          // raw inbound bytes awaiting parse
    QByteArray m_pendingWrites;   // outbound bytes queued before handshake
    bool m_handshakeDone = false;
    quint64 m_nextSid = 1;
    QString m_inboxPrefix;        // "_INBOX.<token>"
    quint64 m_nextInbox = 1;

    // TLS / NGS auth
    bool m_tls = false;
    bool m_encrypted = false;
    QString m_credsPath;
    QByteArray m_nonce;           // server nonce from INFO, signed into CONNECT

    // Frame parser: between a MSG/HMSG control line and its payload we sit in
    // Payload state until totalLen + 2 (trailing CRLF) bytes are buffered.
    enum class State { Control, Payload };
    State m_state = State::Control;
    struct PendingMsg {
        QString subject;
        QString reply;
        quint64 sid = 0;
        int headerLen = 0;   // bytes of header block (HMSG only)
        int totalLen = 0;    // header bytes + payload bytes
        bool hasHeaders = false;
    } m_pending;
};
