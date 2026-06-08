#include "notes/NoteStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>

#include <algorithm>

namespace {
QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool isDeleted(const QJsonObject &meta)
{
    const QJsonValue v = meta.value(QStringLiteral("deleted"));
    return v.toBool() || v.toString() == QLatin1String("true");
}
}  // namespace

NoteStore::NoteStore(QObject *parent)
    : QAbstractListModel(parent)
{
}

void NoteStore::setRootPath(const QString &path)
{
    if (path == m_root)
        return;
    m_root = path;
    reload();
}

void NoteStore::reload()
{
    beginResetModel();
    scan();
    endResetModel();
}

void NoteStore::scan()
{
    m_notes.clear();
    if (m_root.isEmpty())
        return;
    QDir dir(m_root);
    if (!dir.exists())
        return;

    const QStringList metas = dir.entryList({QStringLiteral("*.metadata")}, QDir::Files);

    // First pass: collect folders (CollectionType) so we can rebuild paths, and the
    // candidate documents.
    QHash<QString, QPair<QString, QString>> folders;   // uuid -> (name, parent)
    struct Doc { QString uuid; QJsonObject meta; };
    QVector<Doc> docs;
    for (const QString &m : metas) {
        const QString uuid = QFileInfo(m).completeBaseName();
        const QJsonObject meta = readJson(dir.filePath(m));
        const QString type = meta.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("CollectionType")) {
            folders.insert(uuid, {meta.value(QStringLiteral("visibleName")).toString(),
                                  meta.value(QStringLiteral("parent")).toString()});
        } else if (type == QLatin1String("DocumentType")) {
            docs.append({uuid, meta});
        }
    }

    auto folderPath = [&](QString parent) -> QString {
        QStringList parts;
        int guard = 0;
        while (!parent.isEmpty() && parent != QLatin1String("trash")
               && folders.contains(parent) && guard++ < 32) {
            parts.prepend(folders.value(parent).first);
            parent = folders.value(parent).second;
        }
        return parts.join(QLatin1Char('/'));
    };

    for (const Doc &d : docs) {
        if (isDeleted(d.meta))
            continue;
        const QString parent = d.meta.value(QStringLiteral("parent")).toString();
        if (parent == QLatin1String("trash"))
            continue;
        const QJsonObject content = readJson(dir.filePath(d.uuid + QStringLiteral(".content")));
        if (content.value(QStringLiteral("fileType")).toString() != QLatin1String("notebook"))
            continue;

        Note n;
        n.uuid = d.uuid;
        n.name = d.meta.value(QStringLiteral("visibleName")).toString();
        n.folder = folderPath(parent);
        n.lastModified = d.meta.value(QStringLiteral("lastModified")).toString().toLongLong();

        const QString thumbDir = dir.filePath(d.uuid + QStringLiteral(".thumbnails"));
        const QJsonArray pages =
            content.value(QStringLiteral("cPages")).toObject()
                .value(QStringLiteral("pages")).toArray();
        for (const QJsonValue &pv : pages) {
            const QString pid = pv.toObject().value(QStringLiteral("id")).toString();
            if (pid.isEmpty())
                continue;
            // Thumbnails are rendered lazily — pages never opened recently may have
            // no PNG (some notes have no .thumbnails dir at all). v1 can only attach
            // rendered pages, so skip the rest; notes with none drop out below.
            const QString thumb = thumbDir + QLatin1Char('/') + pid + QStringLiteral(".png");
            if (!QFile::exists(thumb))
                continue;
            n.pages.append({pid, thumb});
        }
        if (!n.pages.isEmpty())
            m_notes.append(n);
    }

    std::sort(m_notes.begin(), m_notes.end(),
              [](const Note &a, const Note &b) { return a.lastModified > b.lastModified; });
}

int NoteStore::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_notes.size();
}

QVariant NoteStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_notes.size())
        return {};
    const Note &n = m_notes[index.row()];
    switch (role) {
    case NameRole:      return n.name;
    case FolderRole:    return n.folder;
    case PageCountRole: return n.pages.size();
    case UuidRole:      return n.uuid;
    default:            return {};
    }
}

QHash<int, QByteArray> NoteStore::roleNames() const
{
    return {
        {NameRole, "name"},
        {FolderRole, "folder"},
        {PageCountRole, "pageCount"},
        {UuidRole, "uuid"},
    };
}

const NoteStore::Note *NoteStore::at(int row) const
{
    if (row < 0 || row >= m_notes.size())
        return nullptr;
    return &m_notes[row];
}
