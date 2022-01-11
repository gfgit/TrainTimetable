#ifndef STATIONSMODEL_H
#define STATIONSMODEL_H

#include "utils/delegates/sql/pageditemmodelhelper.h"

#include "utils/types.h"
#include "stations/station_utils.h"

namespace sqlite3pp {
class query;
}

struct StationModelItem
{
    db_id stationId;
    qint64 phone_number;
    QString name;
    QString shortName;
    utils::StationType type;
};

class StationsModel : public IPagedItemModelImpl<StationsModel, StationModelItem>
{
    Q_OBJECT

public:

    enum { BatchSize = 100 };

    enum Columns{
        NameCol = 0,
        ShortNameCol,
        TypeCol,
        PhoneCol,
        NCols
    };

    typedef StationModelItem StationItem;
    typedef IPagedItemModelImpl<StationsModel, StationModelItem> BaseClass;

    StationsModel(sqlite3pp::database &db, QObject *parent = nullptr);

    // QAbstractTableModel

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &idx, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& idx) const override;

    // IPagedItemModel

    // Sorting
    virtual void setSortingColumn(int col) override;

    //Filter
    std::pair<QString, FilterFlags> getFilterAtCol(int col) override;
    bool setFilterAtCol(int col, const QString& str) override;

    // StationsModel
    bool addStation(const QString& name, db_id *outStationId = nullptr);
    bool removeStation(db_id stationId);

    // Convinience
    inline db_id getIdAtRow(int row) const
    {
        if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
            return 0; //Invalid

        const StationItem& item = cache.at(row - cacheFirstRow);
        return item.stationId;
    }

    inline QString getNameAtRow(int row) const
    {
        if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
            return QString(); //Invalid

        const StationItem& item = cache.at(row - cacheFirstRow);
        return item.name;
    }

protected:
    virtual qint64 recalcTotalItemCount() override;

private:
    friend BaseClass;
    void buildQuery(sqlite3pp::query &q, int sortCol, int offset, bool fullData);
    Q_INVOKABLE void internalFetch(int firstRow, int sortCol, int valRow, const QVariant &val);

    bool setName(StationItem &item, const QString &val);
    bool setShortName(StationItem &item, const QString &val);
    bool setType(StationItem &item, int val);
    bool setPhoneNumber(StationsModel::StationItem &item, qint64 val);

private:
    QString m_nameFilter;
    QString m_phoneFilter;
};

#endif // STATIONSMODEL_H
