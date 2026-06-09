#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// Lists handwritten notebooks from a reMarkable xochitl store, for the attachment
// browser. The store is a flat directory of `<uuid>.metadata` + `<uuid>.content` +
// `<uuid>.thumbnails/<pageId>.png` (see FILE-STORE.md / READING-NOTES.md). v1 attaches
// the device-rendered page thumbnails — no `.rm` parsing.
//
// Notebooks only: `metadata.type == DocumentType` and `content.fileType == notebook`,
// not deleted/trashed. The visible folder path is reconstructed from CollectionType
// parents. A page is listed if it has a `.rm` (the in-app renderer can render it) OR a
// device thumbnail — so v2 attaches any page, not just ones with a rendered thumbnail.
class NoteStore : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Page {
        QString id;
        QString rm;          // absolute path to <pageId>.rm (empty if none on disk)
        QString thumbnail;   // absolute path to the device thumbnail PNG (may be empty)
    };
    struct Note {
        QString uuid;
        QString name;
        QString folder;      // reconstructed "A/B" path, empty at root
        QVector<Page> pages;
        qint64 lastModified = 0;
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        FolderRole,
        PageCountRole,
        UuidRole,
    };

    explicit NoteStore(QObject *parent = nullptr);

    // Directory that contains the `<uuid>.*` entries (device:
    // /home/root/.local/share/remarkable/xochitl; desktop: a copied sample).
    void setRootPath(const QString &path);
    QString rootPath() const { return m_root; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    const Note *at(int row) const;
    int count() const { return m_notes.size(); }

private:
    void scan();
    QString m_root;
    QVector<Note> m_notes;
};
