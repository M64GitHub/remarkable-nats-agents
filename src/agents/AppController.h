#pragma once

#include "agents/AgentModel.h"
#include "agents/AgentProtocol.h"
#include "agents/ChatModel.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QTimer;

// The single QML-facing facade (exposed as the context property `App`). Owns the
// roster and chat models, drives connection state, and translates UI intents
// (connect, select an agent, send a prompt) into AgentProtocol calls — then maps
// the streaming protocol signals back onto the chat model.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *agents READ agents CONSTANT)
    Q_PROPERTY(QObject *messages READ messages CONSTANT)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString selectedAgent READ selectedAgent NOTIFY selectedAgentChanged)
    Q_PROPERTY(bool agentSelected READ agentSelected NOTIFY selectedAgentChanged)
    // NATS CLI contexts found on the system (~/.config/nats/context/*.json).
    Q_PROPERTY(QStringList natsContexts READ natsContexts NOTIFY natsContextsChanged)
    Q_PROPERTY(QString selectedContext READ selectedContext NOTIFY selectedContextChanged)

public:
    explicit AppController(AgentProtocol *protocol, QObject *parent = nullptr);

    QObject *agents() { return &m_agents; }
    QObject *messages() { return &m_chat; }
    QString connectionState() const { return m_connectionState; }
    QString serverUrl() const { return m_serverUrl; }
    QString selectedAgent() const { return m_selectedTitle; }
    bool agentSelected() const { return m_selectedRow >= 0; }
    QStringList natsContexts() const { return m_natsContexts; }
    QString selectedContext() const { return m_selectedContext; }

    // Populate the roster from static config: $AGENT_CHAT_CONFIG, else a local
    // ./agents.json, else the bundled example, else a built-in echo entry.
    void loadRoster();

public slots:
    void connectToServer();                    // dial the configured server URL
    void setServerUrl(const QString &url);     // change + persist the server address
    void useContext(const QString &name);      // apply a NATS context (url + creds)
    void refresh();                            // re-run $SRV discovery
    void selectAgent(int row);                 // choose an agent; resets the chat
    void sendPrompt(const QString &text);      // send to the selected agent

signals:
    void connectionStateChanged();
    void serverUrlChanged();
    void selectedAgentChanged();
    void natsContextsChanged();
    void selectedContextChanged();
    void notice(const QString &message);       // transient, surfaced by the UI

private:
    void setConnectionState(const QString &state);
    bool loadRosterFromJson(const QByteArray &json);
    void scanContexts();                              // populate m_natsContexts
    bool applyContext(const QString &name, bool persist);
    void showStaticRoster();
    void onAgentsDiscovered(const QVector<AgentProtocol::DiscoveredAgent> &agents);
    void onHeartbeat(const QString &instanceId, int intervalS);
    void recomputeLiveness();
    bool isInstanceLive(const QString &instanceId) const;

    AgentProtocol *m_protocol = nullptr;
    AgentModel m_agents;
    ChatModel m_chat;

    QString m_connectionState = QStringLiteral("disconnected");
    QString m_serverUrl = QStringLiteral("nats://127.0.0.1:4222");
    bool m_serverUrlPersisted = false;   // a user-set URL wins over config defaults
    QString m_credsPath;                 // NATS .creds for tls:// (NGS) auth
    bool m_credsFromEnv = false;         // $AGENT_CHAT_CREDS wins over config
    QStringList m_natsContexts;          // available context names
    QString m_selectedContext;           // last applied context (for the UI)
    int m_selectedRow = -1;
    QString m_selectedTitle;
    QString m_selectedSubject;

    // Roster sources: static (config/built-in) shown until/unless discovery finds
    // agents; discovered ones replace it while connected.
    QVector<AgentModel::Entry> m_staticEntries;
    bool m_haveDiscovered = false;

    // Liveness, keyed by $SRV instance id.
    QHash<QString, qint64> m_lastSeenMs;   // last heartbeat, epoch ms
    QHash<QString, int> m_intervalS;       // advertised cadence
    QTimer *m_sweepTimer = nullptr;        // periodic stale check
    QTimer *m_rediscoverTimer = nullptr;   // debounced re-discover on unknown beats
};
