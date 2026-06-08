#include "agents/AgentModel.h"

AgentModel::AgentModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int AgentModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant AgentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries[index.row()];
    switch (role) {
    case TitleRole:
        return e.name;
    case SubtitleRole:
        return QStringLiteral("%1 / %2").arg(e.agent, e.owner);
    case DescriptionRole:
        return e.description;
    case SubjectRole:
        return e.subject;
    case OnlineRole:
        return e.online;
    default:
        return {};
    }
}

QHash<int, QByteArray> AgentModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {DescriptionRole, "description"},
        {SubjectRole, "subject"},
        {OnlineRole, "online"},
    };
}

void AgentModel::setEntries(const QVector<Entry> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

const AgentModel::Entry *AgentModel::at(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return nullptr;
    return &m_entries[row];
}
