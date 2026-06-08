#include "agents/AppController.h"

#include "agents/AgentProtocol.h"
#include "nats/INatsConnection.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTimer>
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
        m_protocol->startHeartbeatWatch();   // subscribe-before-discover (§8.5)
        m_protocol->discover();
        m_sweepTimer->start();
    });
    connect(conn, &INatsConnection::disconnected, this, [this]() {
        setConnectionState(QStringLiteral("disconnected"));
        m_sweepTimer->stop();
        m_lastSeenMs.clear();
        m_intervalS.clear();
        m_haveDiscovered = false;
        showStaticRoster();
    });
    connect(conn, &INatsConnection::errorOccurred, this, [this](const QString &msg) {
        setConnectionState(QStringLiteral("disconnected"));
        emit notice(QStringLiteral("connection error: %1").arg(msg));
    });

    connect(m_protocol, &AgentProtocol::agentsDiscovered, this, &AppController::onAgentsDiscovered);
    connect(m_protocol, &AgentProtocol::heartbeat, this, &AppController::onHeartbeat);

    m_sweepTimer = new QTimer(this);
    m_sweepTimer->setInterval(5000);
    connect(m_sweepTimer, &QTimer::timeout, this, &AppController::recomputeLiveness);

    m_rediscoverTimer = new QTimer(this);   // debounce re-discovery on unknown beats
    m_rediscoverTimer->setSingleShot(true);
    connect(m_rediscoverTimer, &QTimer::timeout, this, [this]() { m_protocol->discover(); });

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
        if (loadRosterFromJson(f.readAll())) {
            showStaticRoster();
            return;
        }
    }

    // Last resort: a single built-in entry that pairs with scripts/echo-responder.py
    // so a fresh checkout can be exercised end-to-end with no config file.
    AgentModel::Entry echo;
    echo.agent = QStringLiteral("echo");
    echo.owner = QStringLiteral("local");
    echo.name = QStringLiteral("test");
    echo.description = QStringLiteral("Local echo responder for testing");
    echo.subject = QStringLiteral("agents.prompt.echo.local.test");
    echo.online = false;   // static entries are unconfirmed until discovery/heartbeat
    m_staticEntries = {echo};
    showStaticRoster();
}

void AppController::showStaticRoster()
{
    m_agents.setEntries(m_staticEntries);
}

bool AppController::loadRosterFromJson(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();

    // Config supplies a default server only when the user hasn't set (persisted) one.
    const QString cfgServer = root.value(QStringLiteral("server")).toString();
    if (!m_serverUrlPersisted && !cfgServer.isEmpty() && cfgServer != m_serverUrl) {
        m_serverUrl = cfgServer;
        emit serverUrlChanged();
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
        e.online = false;   // unconfirmed until discovery/heartbeat says otherwise
        entries.append(e);
    }
    m_staticEntries = entries;
    // A config is a valid roster source if it pins a server OR lists agents — a
    // server-only config (common on the device, where discovery fills the roster)
    // must "win" so we don't fall through to the bundled example's localhost URL.
    return !cfgServer.isEmpty() || !entries.isEmpty();
}

void AppController::connectToServer()
{
    const QUrl url(m_serverUrl);
    const QString host = url.host().isEmpty() ? QStringLiteral("127.0.0.1") : url.host();
    const int port = url.port(4222);
    setConnectionState(QStringLiteral("connecting"));
    m_protocol->connection()->connectToServer(host, static_cast<quint16>(port));
}

void AppController::refresh()
{
    if (m_connectionState == QLatin1String("connected"))
        m_protocol->discover();
    else
        emit notice(QStringLiteral("not connected to NATS"));
}

void AppController::onAgentsDiscovered(const QVector<AgentProtocol::DiscoveredAgent> &agents)
{
    m_haveDiscovered = true;
    if (agents.isEmpty()) {
        emit notice(QStringLiteral("no agents discovered"));
        showStaticRoster();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<AgentModel::Entry> entries;
    for (const AgentProtocol::DiscoveredAgent &a : agents) {
        // One roster row per logical agent (prompt subject); multiple instances
        // behind the "agents" queue group are merged and tracked for liveness.
        int found = -1;
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].subject == a.subject) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            AgentModel::Entry e;
            e.agent = a.agent;
            e.owner = a.owner;
            e.name = a.name;
            e.description = a.description;
            e.subject = a.subject;
            e.instanceIds = {a.instanceId};
            e.online = true;   // it just answered discovery
            entries.append(e);
        } else if (!entries[found].instanceIds.contains(a.instanceId)) {
            entries[found].instanceIds.append(a.instanceId);
        }
        m_lastSeenMs[a.instanceId] = now;   // a discovery reply proves liveness now
        if (!m_intervalS.contains(a.instanceId))
            m_intervalS[a.instanceId] = 30;
    }
    m_agents.setEntries(entries);
    recomputeLiveness();
}

void AppController::onHeartbeat(const QString &instanceId, int intervalS)
{
    m_lastSeenMs[instanceId] = QDateTime::currentMSecsSinceEpoch();
    m_intervalS[instanceId] = intervalS > 0 ? intervalS : 30;

    // If this instance isn't in the roster, a new agent appeared after our last
    // discovery — debounce a re-discovery so it shows up (we subscribe to the
    // heartbeat wildcard before discovering, §8.5, precisely to catch this).
    bool known = false;
    for (int r = 0; r < m_agents.count(); ++r) {
        const AgentModel::Entry *e = m_agents.at(r);
        if (e && e->instanceIds.contains(instanceId)) {
            known = true;
            break;
        }
    }
    if (!known && m_connectionState == QLatin1String("connected"))
        m_rediscoverTimer->start(800);
    else
        recomputeLiveness();
}

void AppController::recomputeLiveness()
{
    for (int r = 0; r < m_agents.count(); ++r) {
        const AgentModel::Entry *e = m_agents.at(r);
        if (!e)
            continue;
        bool live = false;
        for (const QString &id : e->instanceIds) {
            if (isInstanceLive(id)) {
                live = true;
                break;
            }
        }
        m_agents.setOnline(r, live);
    }
}

bool AppController::isInstanceLive(const QString &instanceId) const
{
    const auto it = m_lastSeenMs.constFind(instanceId);
    if (it == m_lastSeenMs.constEnd())
        return false;
    // Offline after 3 missed beats (§8.2).
    const int interval = m_intervalS.value(instanceId, 30);
    const qint64 thresholdMs = qint64(3) * qMax(1, interval) * 1000;
    return (QDateTime::currentMSecsSinceEpoch() - it.value()) <= thresholdMs;
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
