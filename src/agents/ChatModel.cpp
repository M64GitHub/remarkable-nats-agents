#include "agents/ChatModel.h"

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_convs.value(m_curKey).size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    const auto it = m_convs.constFind(m_curKey);
    if (it == m_convs.constEnd())
        return {};
    const QVector<Message> &v = it.value();
    if (index.row() < 0 || index.row() >= v.size())
        return {};
    const Message &m = v[index.row()];
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

void ChatModel::setConversation(const QString &key)
{
    if (key == m_curKey)
        return;
    beginResetModel();
    m_curKey = key;
    m_convs[key];   // ensure the conversation exists so row/data are stable
    endResetModel();
}

int ChatModel::rowForRequest(const QString &key, const QString &requestId) const
{
    const auto it = m_convs.constFind(key);
    if (it == m_convs.constEnd())
        return -1;
    const QVector<Message> &v = it.value();
    for (int i = v.size() - 1; i >= 0; --i)
        if (v[i].requestId == requestId)
            return i;
    return -1;
}

void ChatModel::touch(const QString &key, int row)
{
    if (key != m_curKey)
        return;   // not visible: storage already updated, no view signal needed
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {TextRole, StatusRole});
}

void ChatModel::appendTo(const QString &key, const Message &m)
{
    const bool visible = (key == m_curKey);
    QVector<Message> &v = m_convs[key];
    if (visible)
        beginInsertRows(QModelIndex(), v.size(), v.size());
    v.append(m);
    if (visible)
        endInsertRows();
    trim(key);
}

void ChatModel::trim(const QString &key)
{
    QVector<Message> &v = m_convs[key];
    const bool visible = (key == m_curKey);
    while (v.size() > m_cap) {
        if (visible)
            beginRemoveRows(QModelIndex(), 0, 0);
        if (!v.first().requestId.isEmpty())
            m_reqConv.remove(v.first().requestId);
        v.removeFirst();
        if (visible)
            endRemoveRows();
    }
}

void ChatModel::appendUser(const QString &text)
{
    appendTo(m_curKey, Message{User, text, Done, QString()});
}

void ChatModel::appendAgentPending(const QString &requestId)
{
    m_reqConv.insert(requestId, m_curKey);
    appendTo(m_curKey, Message{Agent, QString(), Pending, requestId});
}

void ChatModel::appendDelta(const QString &requestId, const QString &delta)
{
    const QString key = m_reqConv.value(requestId);
    if (key.isEmpty())
        return;
    const int row = rowForRequest(key, requestId);
    if (row < 0)
        return;
    QVector<Message> &v = m_convs[key];
    v[row].text += delta;
    v[row].status = Streaming;
    touch(key, row);
}

void ChatModel::setDone(const QString &requestId)
{
    const QString key = m_reqConv.value(requestId);
    if (key.isEmpty())
        return;
    const int row = rowForRequest(key, requestId);
    if (row < 0)
        return;
    m_convs[key][row].status = Done;
    touch(key, row);
}

void ChatModel::setError(const QString &requestId, const QString &message)
{
    const QString key = m_reqConv.value(requestId);
    if (key.isEmpty())
        return;
    const int row = rowForRequest(key, requestId);
    if (row < 0)
        return;
    Message &m = m_convs[key][row];
    m.status = Error;
    const QString prefix = QStringLiteral("[error] ");
    m.text = m.text.isEmpty() ? prefix + message
                              : m.text + QStringLiteral("\n") + prefix + message;
    touch(key, row);
}
