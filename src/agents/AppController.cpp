#include "agents/AppController.h"

#include "agents/AgentProtocol.h"
#include "nats/INatsConnection.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSettings>
#include <QUrl>

namespace {
// Accept "host", "host:port", or "nats://host:port" and normalise to a URL the
// QUrl parser in connectToServer() can read. Port defaults to 4222 there.
QString normalizeServerUrl(QString s)
{
    s = s.trimmed();
    if (s.isEmpty())
        return s;
    if (!s.contains(QStringLiteral("://")))
        s.prepend(QStringLiteral("nats://"));
    return s;
}
}  // namespace

AppController::AppController(AgentProtocol *protocol, QObject *parent)
    : QObject(parent), m_protocol(protocol)
{
    INatsConnection *conn = m_protocol->connection();
    connect(conn, &INatsConnection::connected, this, [this]() {
        setConnectionState(QStringLiteral("connected"));
    });
    connect(conn, &INatsConnection::disconnected, this, [this]() {
        setConnectionState(QStringLiteral("disconnected"));
    });
    connect(conn, &INatsConnection::errorOccurred, this, [this](const QString &msg) {
        setConnectionState(QStringLiteral("disconnected"));
        emit notice(QStringLiteral("connection error: %1").arg(msg));
    });

    // Streaming prompt responses flow onto the active conversation.
    connect(m_protocol, &AgentProtocol::promptResponse, this,
            [this](const QString &id, const QString &delta) { m_chat.appendDelta(id, delta); });
    connect(m_protocol, &AgentProtocol::promptComplete, this,
            [this](const QString &id) { m_chat.setDone(id); });
    connect(m_protocol, &AgentProtocol::promptError, this,
            [this](const QString &id, int code, const QString &msg) {
                m_chat.setError(id, code > 0 ? QStringLiteral("%1 (%2)").arg(msg).arg(code) : msg);
            });

    // A server URL the user set earlier (persisted) takes precedence over the
    // config-file/built-in default applied in loadRoster().
    const QString saved = QSettings().value(QStringLiteral("server")).toString();
    if (!saved.isEmpty()) {
        m_serverUrl = saved;
        m_serverUrlPersisted = true;
    }
}

void AppController::setServerUrl(const QString &url)
{
    const QString normalized = normalizeServerUrl(url);
    if (normalized.isEmpty() || normalized == m_serverUrl)
        return;
    m_serverUrl = normalized;
    m_serverUrlPersisted = true;
    QSettings().setValue(QStringLiteral("server"), m_serverUrl);
    emit serverUrlChanged();
}

void AppController::setConnectionState(const QString &state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged();
}

void AppController::loadRoster()
{
    const QString envPath = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("AGENT_CHAT_CONFIG"));
    const QStringList candidates = {
        envPath,
        QStringLiteral("agents.json"),
        QStringLiteral(":/qt/qml/AgentChat/config/agents.example.json"),
    };
    for (const QString &path : candidates) {
        if (path.isEmpty())
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        if (loadRosterFromJson(f.readAll()))
            return;
    }

    // Last resort: a single built-in entry that pairs with scripts/echo-responder.py
    // so a fresh checkout can be exercised end-to-end with no config file.
    AgentModel::Entry echo;
    echo.agent = QStringLiteral("echo");
    echo.owner = QStringLiteral("local");
    echo.name = QStringLiteral("test");
    echo.description = QStringLiteral("Local echo responder for testing");
    echo.subject = QStringLiteral("agents.prompt.echo.local.test");
    m_agents.setEntries({echo});
}

bool AppController::loadRosterFromJson(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();

    // Config supplies a default server only when the user hasn't set (persisted) one.
    if (!m_serverUrlPersisted && root.contains(QStringLiteral("server"))) {
        const QString url = root.value(QStringLiteral("server")).toString();
        if (!url.isEmpty() && url != m_serverUrl) {
            m_serverUrl = url;
            emit serverUrlChanged();
        }
    }

    const QJsonArray arr = root.value(QStringLiteral("agents")).toArray();
    QVector<AgentModel::Entry> entries;
    entries.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        AgentModel::Entry e;
        e.agent = o.value(QStringLiteral("agent")).toString();
        e.owner = o.value(QStringLiteral("owner")).toString();
        e.name = o.value(QStringLiteral("name")).toString();
        e.description = o.value(QStringLiteral("description")).toString();
        e.subject = o.value(QStringLiteral("subject")).toString();
        if (e.subject.isEmpty())
            continue;   // a roster entry with no endpoint subject is useless
        entries.append(e);
    }
    if (entries.isEmpty())
        return false;
    m_agents.setEntries(entries);
    return true;
}

void AppController::connectToServer()
{
    const QUrl url(m_serverUrl);
    const QString host = url.host().isEmpty() ? QStringLiteral("127.0.0.1") : url.host();
    const int port = url.port(4222);
    setConnectionState(QStringLiteral("connecting"));
    m_protocol->connection()->connectToServer(host, static_cast<quint16>(port));
}

void AppController::selectAgent(int row)
{
    const AgentModel::Entry *e = m_agents.at(row);
    if (!e)
        return;
    m_selectedRow = row;
    m_selectedTitle = e->name;
    m_selectedSubject = e->subject;
    m_chat.clear();
    emit selectedAgentChanged();
}

void AppController::sendPrompt(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    if (m_selectedRow < 0) {
        emit notice(QStringLiteral("select an agent first"));
        return;
    }
    if (m_connectionState != QLatin1String("connected")) {
        emit notice(QStringLiteral("not connected to NATS"));
        return;
    }
    const QString id = m_protocol->sendPrompt(m_selectedSubject, trimmed);
    if (id.isEmpty()) {
        emit notice(QStringLiteral("could not send prompt"));
        return;
    }
    m_chat.appendUser(trimmed);
    m_chat.appendAgentPending(id);
}
