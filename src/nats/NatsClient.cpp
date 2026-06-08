#include "nats/NatsClient.h"

#include "nats/NatsCreds.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRandomGenerator>
#include <QSslError>
#include <QStringList>
#include <QtGlobal>

NatsClient::NatsClient(QObject *parent)
    : INatsConnection(parent)
{
    connect(&m_socket, &QSslSocket::connected, this, &NatsClient::onSocketConnected);
    connect(&m_socket, &QSslSocket::encrypted, this, &NatsClient::onEncrypted);
    connect(&m_socket, &QSslSocket::readyRead, this, &NatsClient::onReadyRead);
    connect(&m_socket, &QSslSocket::errorOccurred, this, &NatsClient::onSocketError);
    connect(&m_socket, &QSslSocket::sslErrors, this,
            [this](const QList<QSslError> &errors) {
                QStringList msgs;
                for (const QSslError &e : errors)
                    msgs << e.errorString();
                // Don't ignore — a failed cert check should surface, not silently pass.
                emit errorOccurred(QStringLiteral("TLS: %1").arg(msgs.join(QStringLiteral("; "))));
            });
    connect(&m_socket, &QSslSocket::disconnected, this, [this]() {
        m_handshakeDone = false;
        m_encrypted = false;
        m_buffer.clear();
        m_pendingWrites.clear();
        m_state = State::Control;
        emit disconnected();
    });
}

void NatsClient::connectToServer(const QString &host, quint16 port, bool tls,
                                 const QString &credsPath)
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.abort();
    m_handshakeDone = false;
    m_encrypted = false;
    m_tls = tls;
    m_credsPath = credsPath;
    m_nonce.clear();
    m_buffer.clear();
    m_pendingWrites.clear();
    m_state = State::Control;

    // A per-connection inbox prefix so reply subjects from a previous connection
    // can never collide with this one.
    const quint64 r = QRandomGenerator::global()->generate64();
    m_inboxPrefix = QStringLiteral("_INBOX.%1").arg(r, 16, 16, QLatin1Char('0'));
    m_nextInbox = 1;

    m_socket.connectToHost(host, port);
}

void NatsClient::disconnectFromServer()
{
    m_socket.disconnectFromHost();
}

bool NatsClient::isConnected() const
{
    return m_handshakeDone && m_socket.state() == QAbstractSocket::ConnectedState;
}

void NatsClient::onSocketConnected()
{
    // Don't emit connected() yet — we wait for the server INFO line; then either
    // upgrade to TLS (handleControl) or send CONNECT (completeHandshake).
}

void NatsClient::onEncrypted()
{
    // TLS handshake done — now it's safe to send CONNECT (with the signed nonce).
    m_encrypted = true;
    completeHandshake();
}

void NatsClient::onSocketError(QAbstractSocket::SocketError)
{
    emit errorOccurred(m_socket.errorString());
}

void NatsClient::write(const QByteArray &bytes)
{
    if (m_handshakeDone) {
        m_socket.write(bytes);
    } else {
        m_pendingWrites.append(bytes);   // flushed by completeHandshake()
    }
}

void NatsClient::completeHandshake()
{
    // verbose=false  -> no +OK acks to parse.
    // headers=true   -> server may send HMSG; required to see service errors and
    //                   to distinguish the headerless stream terminator (§6.5).
    // no_responders  -> a request to a subject with no subscribers gets an
    //                   immediate 503 reply instead of hanging until timeout.
    QJsonObject opts{
        {QStringLiteral("verbose"), false},
        {QStringLiteral("pedantic"), false},
        {QStringLiteral("tls_required"), m_tls},
        {QStringLiteral("name"), QStringLiteral("agent-chat")},
        {QStringLiteral("lang"), QStringLiteral("cpp")},
        {QStringLiteral("version"), QStringLiteral("0.1")},
        {QStringLiteral("protocol"), 1},
        {QStringLiteral("headers"), true},
        {QStringLiteral("no_responders"), true},
    };

    // NGS / decentralized auth: sign the server nonce with the user NKEY seed and
    // present the user JWT (§10). Loaded fresh so the secret isn't held long-term.
    if (!m_credsPath.isEmpty()) {
        NatsCreds creds;
        if (creds.loadFromFile(m_credsPath) && creds.isValid()) {
            opts.insert(QStringLiteral("jwt"), creds.jwt());
            opts.insert(QStringLiteral("sig"),
                        QString::fromLatin1(creds.signNonce(m_nonce)));
        } else {
            emit errorOccurred(QStringLiteral("could not load NATS credentials: %1")
                                   .arg(m_credsPath));
        }
    }

    QByteArray cmd = "CONNECT " + QJsonDocument(opts).toJson(QJsonDocument::Compact) + "\r\n";
    m_socket.write(cmd);
    m_handshakeDone = true;
    if (!m_pendingWrites.isEmpty()) {
        m_socket.write(m_pendingWrites);
        m_pendingWrites.clear();
    }
    emit connected();
}

quint64 NatsClient::subscribe(const QString &subject, const QString &queue)
{
    const quint64 sid = m_nextSid++;
    QByteArray cmd = "SUB " + subject.toUtf8() + ' ';
    if (!queue.isEmpty())
        cmd += queue.toUtf8() + ' ';
    cmd += QByteArray::number(sid) + "\r\n";
    write(cmd);
    return sid;
}

void NatsClient::unsubscribe(quint64 sid, int maxMsgs)
{
    QByteArray cmd = "UNSUB " + QByteArray::number(sid);
    if (maxMsgs > 0)
        cmd += ' ' + QByteArray::number(maxMsgs);
    cmd += "\r\n";
    write(cmd);
}

void NatsClient::publish(const QString &subject, const QByteArray &payload, const QString &replyTo)
{
    QByteArray cmd = "PUB " + subject.toUtf8() + ' ';
    if (!replyTo.isEmpty())
        cmd += replyTo.toUtf8() + ' ';
    cmd += QByteArray::number(payload.size()) + "\r\n";
    cmd += payload;
    cmd += "\r\n";
    write(cmd);
}

QString NatsClient::newInbox()
{
    return QStringLiteral("%1.%2").arg(m_inboxPrefix).arg(m_nextInbox++);
}

void NatsClient::onReadyRead()
{
    m_buffer.append(m_socket.readAll());
    processBuffer();
}

bool NatsClient::readControlLine(QByteArray &line)
{
    const int idx = m_buffer.indexOf("\r\n");
    if (idx < 0)
        return false;
    line = m_buffer.left(idx);
    m_buffer.remove(0, idx + 2);
    return true;
}

void NatsClient::processBuffer()
{
    for (;;) {
        if (m_state == State::Control) {
            QByteArray line;
            if (!readControlLine(line))
                return;             // need more bytes
            if (line.isEmpty())
                continue;
            handleControl(line);
            // handleControl may flip us into Payload state.
        } else {
            // Payload state: wait for totalLen bytes + trailing CRLF.
            if (m_buffer.size() < m_pending.totalLen + 2)
                return;
            deliverPending();
            m_state = State::Control;
        }
    }
}

void NatsClient::handleControl(const QByteArray &line)
{
    // Fast dispatch on the verb. Control verbs are ASCII and case-insensitive
    // per the protocol; servers emit them uppercase.
    if (line.startsWith("MSG ") || line.startsWith("HMSG ")) {
        const bool hmsg = line.startsWith("HMSG ");
        const QList<QByteArray> t =
            line.mid(hmsg ? 5 : 4).simplified().split(' ');
        // MSG  <subject> <sid> [reply] <#bytes>
        // HMSG <subject> <sid> [reply] <#headerbytes> <#totalbytes>
        m_pending = PendingMsg{};
        m_pending.hasHeaders = hmsg;
        if (hmsg) {
            if (t.size() == 4) {            // no reply
                m_pending.subject = QString::fromUtf8(t[0]);
                m_pending.sid = t[1].toULongLong();
                m_pending.headerLen = t[2].toInt();
                m_pending.totalLen = t[3].toInt();
            } else if (t.size() >= 5) {     // with reply
                m_pending.subject = QString::fromUtf8(t[0]);
                m_pending.sid = t[1].toULongLong();
                m_pending.reply = QString::fromUtf8(t[2]);
                m_pending.headerLen = t[3].toInt();
                m_pending.totalLen = t[4].toInt();
            }
        } else {
            if (t.size() == 3) {            // no reply
                m_pending.subject = QString::fromUtf8(t[0]);
                m_pending.sid = t[1].toULongLong();
                m_pending.totalLen = t[2].toInt();
            } else if (t.size() >= 4) {     // with reply
                m_pending.subject = QString::fromUtf8(t[0]);
                m_pending.sid = t[1].toULongLong();
                m_pending.reply = QString::fromUtf8(t[2]);
                m_pending.totalLen = t[3].toInt();
            }
        }
        m_state = State::Payload;
        return;
    }
    if (line == "PING") {
        m_socket.write("PONG\r\n");
        return;
    }
    if (line == "PONG")
        return;
    if (line.startsWith("INFO")) {
        const QJsonObject info =
            QJsonDocument::fromJson(line.mid(4).trimmed()).object();
        m_nonce = info.value(QStringLiteral("nonce")).toString().toUtf8();
        const bool serverWantsTls = info.value(QStringLiteral("tls_required")).toBool();
        if ((m_tls || serverWantsTls) && !m_encrypted) {
            // Upgrade now; CONNECT is sent from onEncrypted() once TLS is up.
            m_socket.startClientEncryption();
        } else if (!m_handshakeDone) {
            completeHandshake();
        }
        return;
    }
    if (line.startsWith("+OK"))
        return;
    if (line.startsWith("-ERR")) {
        emit errorOccurred(QString::fromUtf8(line.mid(4).trimmed()));
        return;
    }
    // Unknown control line — ignore (forward-compat).
}

void NatsClient::deliverPending()
{
    QByteArray frame = m_buffer.left(m_pending.totalLen);
    m_buffer.remove(0, m_pending.totalLen + 2);   // drop payload + trailing CRLF

    QByteArray payload;
    QVariantMap headers;
    if (m_pending.hasHeaders) {
        headers = parseHeaders(frame.left(m_pending.headerLen));
        payload = frame.mid(m_pending.headerLen);
    } else {
        payload = frame;
    }
    emit messageReceived(m_pending.sid, m_pending.subject, m_pending.reply, payload, headers);
}

QVariantMap NatsClient::parseHeaders(const QByteArray &block)
{
    // Header block: "NATS/1.0[ <code> <text>]\r\nKey: Value\r\n...\r\n\r\n".
    QVariantMap out;
    const QList<QByteArray> lines = block.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        QByteArray l = lines[i];
        if (l.endsWith('\r'))
            l.chop(1);
        if (l.isEmpty())
            continue;
        if (i == 0) {
            // Status line. Capture an inline status code (e.g. 503 no-responders)
            // so the protocol layer can treat it as an error.
            const QList<QByteArray> parts = l.simplified().split(' ');
            if (parts.size() >= 2) {
                bool ok = false;
                const int code = parts[1].toInt(&ok);
                if (ok)
                    out.insert(QStringLiteral("Status-Code"), code);
            }
            continue;
        }
        const int colon = l.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = QString::fromUtf8(l.left(colon)).trimmed();
        const QString val = QString::fromUtf8(l.mid(colon + 1)).trimmed();
        out.insert(key, val);
    }
    return out;
}
