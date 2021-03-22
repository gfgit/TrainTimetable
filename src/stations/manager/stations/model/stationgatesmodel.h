#ifndef STATIONGATESMODEL_H
#define STATIONGATESMODEL_H

#include "utils/sqldelegate/pageditemmodel.h"

#include "stations/station_utils.h"

class StationGatesModel : public IPagedItemModel
{
    Q_OBJECT

public:

    enum { BatchSize = 100 };

    typedef enum {
        LetterCol = 0,
        SideCol,
        OutTrackCountCol,
        DefaultInPlatfCol,
        IsEntranceCol,
        IsExitCol,
        NCols
    } Columns;

    typedef struct GateItem_
    {
        db_id gateId;
        db_id defaultInPlatfId;
        int outTrackCount;
        QChar letter;
        QString defPlatfName;
        utils::GateSide side;
        QFlags<utils::GateType> type;
    } GateItem;

    StationGatesModel(sqlite3pp::database &db, QObject *parent = nullptr);

    bool event(QEvent *e) override;

    // QAbstractTableModel

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &idx, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& idx) const override;

    // IPagedItemModel

    // Cached rows management
    virtual void clearCache() override;
    virtual void refreshData() override;

    // Sorting TODO: enable multiple columns sort/filter with custom QHeaderView
    virtual void setSortingColumn(int col) override;

    // StationGatesModel
    bool setStation(db_id stationId);
    bool addGate(const QChar& name, db_id *outGateId = nullptr);
    bool removeGate(db_id stationId);

    // Convinience
    inline db_id getIdAtRow(int row) const
    {
        if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
            return 0; //Invalid

        const GateItem& item = cache.at(row - cacheFirstRow);
        return item.gateId;
    }

    inline QChar getNameAtRow(int row) const
    {
        if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
            return QChar(); //Invalid

        const GateItem& item = cache.at(row - cacheFirstRow);
        return item.letter;
    }

    inline bool isEditable() const { return editable; }
    inline void setEditable(bool val) { editable = val; }

private:
    void fetchRow(int row);
    Q_INVOKABLE void internalFetch(int firstRow, int sortCol, int valRow, const QVariant &val);
    void handleResult(const QVector<GateItem> &items, int firstRow);

    bool setName(GateItem &item, const QChar &val);
    bool setSide(GateItem &item, int val);
    bool setType(GateItem &item, QFlags<utils::GateType> type);
    bool setOutTrackCount(GateItem &item, int count);

private:
    db_id m_stationId;
    QVector<GateItem> cache;
    int cacheFirstRow;
    int firstPendingRow;
    bool editable;
};

#endif // STATIONGATESMODEL_H