#pragma once

#include "core/Types.h"

#include <QAbstractTableModel>

namespace eis::widgets {

class ArchiveTreeModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { PathColumn = 0, SizeColumn, PackedColumn, TypeColumn, EncryptedColumn, ColumnCount };

    explicit ArchiveTreeModel(QObject *parent = nullptr);

    void setListing(const eis::ArchiveListing &listing);
    const eis::ArchiveListing &listing() const { return m_listing; }
    eis::ArchiveEntryInfo entryAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    eis::ArchiveListing m_listing;
};

} // namespace eis::widgets
