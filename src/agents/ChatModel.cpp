#include "agents/ChatModel.h"

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_messages.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size())
        return {};
    const Message &m = m_messages[index.row()];
    switch (role) {
    case TextRole:
        return m.text;
    case IsUserRole:
        return m.role == User;
    case StatusRole:
        switch (m.status) {
        case Pending:   return QStringLiteral("pending");
        case Streaming: return QStringLiteral("streaming");
        case Done:      return QStringLiteral("done");
        case Error:     return QStringLiteral("error");
        }
        return {};
    default:
        return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {
        {TextRole, "text"},
        {IsUserRole, "isUser"},
        {StatusRole, "status"},
    };
}

void ChatModel::clear()
{
    if (m_messages.isEmpty())
        return;
    beginResetModel();
    m_messages.clear();
    endResetModel();
}

void ChatModel::appendUser(const QString &text)
{
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(Message{User, text, Done, QString()});
    endInsertRows();
}

void ChatModel::appendAgentPending(const QString &requestId)
{
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(Message{Agent, QString(), Pending, requestId});
    endInsertRows();
}

int ChatModel::rowForRequest(const QString &requestId) const
{
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].requestId == requestId)
            return i;
    }
    return -1;
}

void ChatModel::emitRowChanged(int row)
{
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {TextRole, StatusRole});
}

void ChatModel::appendDelta(const QString &requestId, const QString &delta)
{
    const int row = rowForRequest(requestId);
    if (row < 0)
        return;
    m_messages[row].text += delta;
    m_messages[row].status = Streaming;
    emitRowChanged(row);
}

void ChatModel::setDone(const QString &requestId)
{
    const int row = rowForRequest(requestId);
    if (row < 0)
        return;
    // An empty-but-complete reply still reads as done, not pending.
    m_messages[row].status = Done;
    emitRowChanged(row);
}

void ChatModel::setError(const QString &requestId, const QString &message)
{
    const int row = rowForRequest(requestId);
    if (row < 0)
        return;
    Message &m = m_messages[row];
    m.status = Error;
    const QString prefix = QStringLiteral("[error] ");
    m.text = m.text.isEmpty() ? prefix + message
                              : m.text + QStringLiteral("\n") + prefix + message;
    emitRowChanged(row);
}
