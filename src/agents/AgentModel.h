#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVector>

// Roster of agents shown in the left/first screen. In M1 this is populated from
// static config (AppController::loadRoster); in M2 it is refreshed from $SRV
// discovery and heartbeat liveness. The `subject` is the prompt endpoint subject
// a caller publishes to — learned from discovery in M2, declared in config now.
class AgentModel : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Entry {
        QString agent;        // metadata.agent, e.g. "claude-code"
        QString owner;        // metadata.owner
        QString name;         // instance name (5th subject token)
        QString description;  // human-readable
        QString subject;      // prompt endpoint subject to publish to
        QStringList instanceIds;  // $SRV instance ids behind this subject (liveness)
        bool online = true;   // M1: assumed; M2: from heartbeats
        bool attachmentsOk = false;  // endpoint accepts attachments (§2.1)
        int maxPayloadBytes = 0;     // endpoint max_payload (0 = unknown)
    };

    enum Roles {
        TitleRole = Qt::UserRole + 1,   // "name" — primary label
        SubtitleRole,                   // "agent/owner" identity line
        DescriptionRole,
        SubjectRole,
        OnlineRole,
    };

    explicit AgentModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const QVector<Entry> &entries);
    const Entry *at(int row) const;
    int count() const { return m_entries.size(); }

    // Toggle a row's liveness without resetting the model (emits dataChanged).
    void setOnline(int row, bool online);

private:
    QVector<Entry> m_entries;
};
