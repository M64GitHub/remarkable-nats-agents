#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

// Per-agent conversations. The model exposes ONE conversation at a time (the
// selected agent, keyed by its prompt subject), but retains every conversation
// for the session — so switching agents and coming back preserves history. Each
// conversation is capped at the most recent `m_cap` messages.
//
// Streaming replies are routed by request id to the right conversation even if
// it isn't the one on screen, so a reply that lands after you've switched agents
// still updates its own history.
class ChatModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { User, Agent };
    enum Status { Pending, Streaming, Done, Error };

    enum Roles {
        TextRole = Qt::UserRole + 1,
        IsUserRole,
        StatusRole,   // "pending" | "streaming" | "done" | "error"
    };

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Show the conversation for `key` (an agent's prompt subject). Retained
    // history for that key reappears; an unseen key starts empty.
    void setConversation(const QString &key);

    void appendUser(const QString &text);
    void appendAgentPending(const QString &requestId);
    void appendDelta(const QString &requestId, const QString &delta);
    void setDone(const QString &requestId);
    void setError(const QString &requestId, const QString &message);

private:
    struct Message {
        Role role = Agent;
        QString text;
        Status status = Done;
        QString requestId;   // agent rows only
    };

    int rowForRequest(const QString &key, const QString &requestId) const;
    void appendTo(const QString &key, const Message &m);
    void trim(const QString &key);
    void touch(const QString &key, int row);   // dataChanged, only if key is visible

    QHash<QString, QVector<Message>> m_convs;   // subject -> messages
    QHash<QString, QString> m_reqConv;          // requestId -> subject
    QString m_curKey;                            // visible conversation
    int m_cap = 20;                              // max retained/shown per conversation
};
