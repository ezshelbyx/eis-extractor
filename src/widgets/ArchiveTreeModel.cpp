#include "widgets/ArchiveTreeModel.h"

#include "core/Utils.h"

#include <QIcon>

namespace eis::widgets {

ArchiveTreeModel::ArchiveTreeModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ArchiveTreeModel::setListing(const eis::ArchiveListing &listing)
{
    beginResetModel();
    m_listing = listing;
    endResetModel();
}

eis::ArchiveEntryInfo ArchiveTreeModel::entryAt(int row) const
{
    if (row < 0 || row >= m_listing.entries.size())
        return eis::ArchiveEntryInfo();
    return m_listing.entries.at(row);
}

int ArchiveTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_listing.entries.size();
}

int ArchiveTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return ColumnCount;
}

QVariant ArchiveTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_listing.entries.size())
        return QVariant();
    const eis::ArchiveEntryInfo &e = m_listing.entries.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case PathColumn:
            return e.path;
        case SizeColumn:
            return e.size >= 0 ? eis::utils::humanBytes(e.size) : QStringLiteral("--");
        case PackedColumn:
            return e.compressedSize >= 0 ? eis::utils::humanBytes(e.compressedSize) : QStringLiteral("--");
        case TypeColumn:
            return e.isDir ? QStringLiteral("dir") : (e.isSymlink ? QStringLiteral("symlink") : (e.isHardlink ? QStringLiteral("hardlink") : e.typeName));
        case EncryptedColumn:
            return e.encrypted ? QStringLiteral("yes") : QStringLiteral("no");
        }
    }
    if (role == Qt::ToolTipRole && index.column() == PathColumn) {
        return e.path;
    }
    return QVariant();
}

QVariant ArchiveTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
    case PathColumn:
        return QStringLiteral("Path");
    case SizeColumn:
        return QStringLiteral("Size");
    case PackedColumn:
        return QStringLiteral("Packed");
    case TypeColumn:
        return QStringLiteral("Type");
    case EncryptedColumn:
        return QStringLiteral("Encrypted");
    }
    return QVariant();
}

} // namespace eis::widgets
