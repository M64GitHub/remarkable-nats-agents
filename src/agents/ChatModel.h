#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// The active conversation. User messages and agent replies are appended in
// order. An agent reply starts as a pending row tagged with the protocol request
// id; streaming `response` chunks append to its text, and the terminator (or an
// error) flips its status. The view renders one conversation at a time; M1 keeps
// a single conversation that resets when a different agent is selected.
class ChatModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { User, Agent };
    enum Status { Pending, Streaming, Done, Error };

    enum Roles {
        TextRole = Qt::UserRole + 1,
        IsUserRole,
        StatusRole,   // one of "pending" | "streaming" | "done" | "error"
    };

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void appendUser(const QString &text);
    // Append an empty, pending agent row tagged with `requestId`.
    void appendAgentPending(const QString &requestId);
    // Append `delta` to the agent row for `requestId` and mark it streaming.
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
    int rowForRequest(const QString &requestId) const;
    void emitRowChanged(int row);

    QVector<Message> m_messages;
};
