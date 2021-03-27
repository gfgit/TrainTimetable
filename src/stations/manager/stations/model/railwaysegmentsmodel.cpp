#include "railwaysegmentsmodel.h"

#include <QCoreApplication>
#include <QEvent>

#include <sqlite3pp/sqlite3pp.h>
using namespace sqlite3pp;

#include "utils/worker_event_types.h"

#include "stations/station_name_utils.h"

#include <QDebug>

class RailwaySegmentsResultEvent : public QEvent
{
public:
    static constexpr Type _Type = Type(CustomEvents::RailwayNodeModelResult);
    inline RailwaySegmentsResultEvent() : QEvent(_Type) {}

    QVector<RailwaySegmentsModel::RailwaySegmentItem> items;
    int firstRow;
};

RailwaySegmentsModel::RailwaySegmentsModel(sqlite3pp::database &db, QObject *parent) :
    IPagedItemModel(500, db, parent),
    cacheFirstRow(0),
    firstPendingRow(-BatchSize),
    filterFromStationId(0)
{
    //TODO: connect to StationsModel stationNameChanged
    sortColumn = NameCol;
}

bool RailwaySegmentsModel::event(QEvent *e)
{
    if(e->type() == RailwaySegmentsResultEvent::_Type)
    {
        RailwaySegmentsResultEvent *ev = static_cast<RailwaySegmentsResultEvent *>(e);
        ev->setAccepted(true);

        handleResult(ev->items, ev->firstRow);

        return true;
    }

    return QAbstractTableModel::event(e);
}

QVariant RailwaySegmentsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        {
            switch (section)
            {
            case NameCol:
                return tr("Name");
            case FromStationCol:
                return tr("From");
            case FromGateCol:
                return tr("Gate");
            case ToStationCol:
                return tr("To");
            case ToGateCol:
                return tr("Gate");
            case MaxSpeedCol:
                return tr("Max. Speed");
            case DistanceCol:
                return tr("Distance");
            case IsElectrifiedCol:
                return tr("Electrified");
            }
            break;
        }
        }
    }
    else if(role == Qt::DisplayRole)
    {
        return section + curPage * ItemsPerPage + 1;
    }

    return QAbstractTableModel::headerData(section, orientation, role);
}

int RailwaySegmentsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : curItemCount;
}

int RailwaySegmentsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : NCols;
}

QVariant RailwaySegmentsModel::data(const QModelIndex &idx, int role) const
{
    const int row = idx.row();
    if (!idx.isValid() || row >= curItemCount || idx.column() >= NCols)
        return QVariant();

    if(row < cacheFirstRow || row >= cacheFirstRow + cache.size())
    {
        //Fetch above or below current cache
        const_cast<RailwaySegmentsModel *>(this)->fetchRow(row);

        //Temporarily return null
        return role == Qt::DisplayRole ? QVariant("...") : QVariant();
    }

    const RailwaySegmentItem& item = cache.at(row - cacheFirstRow);

    switch (role)
    {
    case Qt::DisplayRole:
    {
        switch (idx.column())
        {
        case NameCol:
            return item.segmentName;
        case FromStationCol:
            return item.fromStationName;
        case FromGateCol:
            return item.fromGateLetter;
        case ToStationCol:
            return item.toStationName;
        case ToGateCol:
            return item.toGateLetter;
        case MaxSpeedCol:
            return QStringLiteral("%1 km/h").arg(item.maxSpeedKmH);
        case DistanceCol:
            return item.distanceMeters; //TODO: better format
        }
        break;
    }
    case Qt::CheckStateRole:
    {
        switch (idx.column())
        {
        case IsElectrifiedCol:
            return item.type.testFlag(utils::RailwaySegmentType::Electrified) ? Qt::Checked : Qt::Unchecked;
        }
        break;
    }
    }

    return QVariant();
}

bool RailwaySegmentsModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    //TODO: edit here or always by the dialog?
    return false;
}

Qt::ItemFlags RailwaySegmentsModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
    if(idx.row() < cacheFirstRow || idx.row() >= cacheFirstRow + cache.size())
        return f; //Not fetched yet

    if(idx.column() == IsElectrifiedCol)
        f.setFlag(Qt::ItemIsUserCheckable);

    return f;
}

void RailwaySegmentsModel::clearCache()
{
    cache.clear();
    cache.squeeze();
    cacheFirstRow = 0;
}

void RailwaySegmentsModel::refreshData()
{
    if(!mDb.db())
        return;

    emit itemsReady(-1, -1); //Notify we are about to refresh

    query q(mDb);
    if(filterFromStationId)
    {
        q.prepare("SELECT COUNT(1) FROM railway_segments s"
                  " JOIN station_gates g1 ON g1.id=s.in_gate_id"
                  " JOIN station_gates g2 ON g2.id=s.out_gate_id"
                  " WHERE g1.station_id=?1 OR g2.station_id=?1");
        q.bind(1, filterFromStationId);
    }else{
        q.prepare("SELECT COUNT(1) FROM railway_segments");
    }

    q.step();
    const int count = q.getRows().get<int>(0);
    if(count != totalItemsCount)
    {
        beginResetModel();

        clearCache();
        totalItemsCount = count;
        emit totalItemsCountChanged(totalItemsCount);

        //Round up division
        const int rem = count % ItemsPerPage;
        pageCount = count / ItemsPerPage + (rem != 0);
        emit pageCountChanged(pageCount);

        if(curPage >= pageCount)
        {
            switchToPage(pageCount - 1);
        }

        curItemCount = totalItemsCount ? (curPage == pageCount - 1 && rem) ? rem : ItemsPerPage : 0;

        endResetModel();
    }
}

void RailwaySegmentsModel::setSortingColumn(int col)
{
    if(sortColumn == col || (col != FromGateCol && col != ToGateCol && col != IsElectrifiedCol))
        return;

    clearCache();
    sortColumn = col;

    QModelIndex first = index(0, 0);
    QModelIndex last = index(curItemCount - 1, NCols - 1);
    emit dataChanged(first, last);
}

void RailwaySegmentsModel::fetchRow(int row)
{
    if(firstPendingRow != -BatchSize)
        return; //Currently fetching another batch, wait for it to finish first

    if(row >= firstPendingRow && row < firstPendingRow + BatchSize)
        return; //Already fetching this batch

    if(row >= cacheFirstRow && row < cacheFirstRow + cache.size())
        return; //Already cached

    //TODO: abort fetching here

    const int remainder = row % BatchSize;
    firstPendingRow = row - remainder;
    qDebug() << "Requested:" << row << "From:" << firstPendingRow;

    QVariant val;
    int valRow = 0;


    //TODO: use a custom QRunnable
    //    QMetaObject::invokeMethod(this, "internalFetch", Qt::QueuedConnection,
    //                              Q_ARG(int, firstPendingRow), Q_ARG(int, sortCol),
    //                              Q_ARG(int, valRow), Q_ARG(QVariant, val));
    internalFetch(firstPendingRow, sortColumn, val.isNull() ? 0 : valRow, val);
}

void RailwaySegmentsModel::internalFetch(int first, int sortCol, int valRow, const QVariant &val)
{
    query q(mDb);

    int offset = first - valRow + curPage * ItemsPerPage;
    bool reverse = false;

    if(valRow > first)
    {
        offset = 0;
        reverse = true;
    }

    qDebug() << "Fetching:" << first << "ValRow:" << valRow << val << "Offset:" << offset << "Reverse:" << reverse;

    const char *whereCol = nullptr;

    QByteArray sql = "SELECT s.id, s.name, s.max_speed_kmh, s.type, s.distance_meters,"
                     "g1.station_id, g2.station_id,"
                     "s.in_gate_id, g1.name, st1.name,"
                     "s.out_gate_id,g2.name, st2.name"
                     " FROM railway_segments s"
                     " JOIN station_gates g1 ON g1.id=s.in_gate_id"
                     " JOIN station_gates g2 ON g2.id=s.out_gate_id"
                     " JOIN stations st1 ON st1.id=g1.station_id"
                     " JOIN stations st2 ON st2.id=g2.station_id";
    switch (sortCol)
    {
    case NameCol:
    {
        whereCol = "s.name"; //Order by 1 column, no where clause
        break;
    }
    case FromStationCol:
    {
        whereCol = "st1.name";
        break;
    }
    case ToStationCol:
    {
        whereCol = "st2.name";
        break;
    }
    case MaxSpeedCol:
    {
        whereCol = "s.max_speed_kmh";
        break;
    }
    case DistanceCol:
    {
        whereCol = "s.distance_meters";
        break;
    }
    }

    if(val.isValid())
    {
        sql += " WHERE ";
        sql += whereCol;
        if(reverse)
            sql += "<?3";
        else
            sql += ">?3";
    }

    if(filterFromStationId)
    {
        sql += " WHERE g1.station_id=?4 OR g2.station_id=?4";
    }

    sql += " ORDER BY ";
    sql += whereCol;

    if(reverse)
        sql += " DESC";

    sql += " LIMIT ?1";
    if(offset)
        sql += " OFFSET ?2";

    q.prepare(sql);
    q.bind(1, BatchSize);
    if(offset)
        q.bind(2, offset);

    if(filterFromStationId)
        q.bind(4, filterFromStationId);

    //    if(val.isValid())
    //    {
    //        switch (sortCol)
    //        {
    //        case LineNameCol:
    //        {
    //            q.bind(3, val.toString());
    //            break;
    //        }
    //        }
    //    }

    QVector<RailwaySegmentItem> vec(BatchSize);

    auto it = q.begin();
    const auto end = q.end();

    if(reverse)
    {
        int i = BatchSize - 1;

        for(; it != end; ++it)
        {
            auto r = *it;
            RailwaySegmentItem &item = vec[i];
            item.segmentId = r.get<db_id>(0);
            item.segmentName = r.get<QString>(1);
            item.maxSpeedKmH = r.get<int>(2);
            item.type = utils::RailwaySegmentType(r.get<int>(3));
            item.distanceMeters = r.get<int>(4);

            item.fromStationId = r.get<db_id>(5);
            item.toStationId = r.get<db_id>(6);

            item.fromGateId = r.get<db_id>(7);
            item.fromGateLetter = sqlite3_column_text(q.stmt(), 8)[0];
            item.fromStationName = r.get<QString>(9);

            item.fromGateId = r.get<db_id>(10);
            item.fromGateLetter = sqlite3_column_text(q.stmt(), 11)[0];
            item.fromStationName = r.get<QString>(12);
            item.reversed = false;

            if(filterFromStationId)
            {
                if(filterFromStationId == item.toStationId)
                {
                    //Always show filter station as 'From'
                    qSwap(item.fromStationId, item.toStationId);
                    qSwap(item.fromGateId, item.toGateId);
                    qSwap(item.fromStationName, item.toStationName);
                    qSwap(item.fromGateLetter, item.toGateLetter);
                    item.reversed = true;
                }
                //item.fromStationName.clear(); //Save some memory???
            }
            i--;
        }
        if(i > -1)
            vec.remove(0, i + 1);
    }
    else
    {
        int i = 0;

        for(; it != end; ++it)
        {
            auto r = *it;
            RailwaySegmentItem &item = vec[i];
            item.segmentId = r.get<db_id>(0);
            item.segmentName = r.get<QString>(1);
            item.maxSpeedKmH = r.get<int>(2);
            item.type = utils::RailwaySegmentType(r.get<int>(3));
            item.distanceMeters = r.get<int>(4);

            item.fromStationId = r.get<db_id>(5);
            item.toStationId = r.get<db_id>(6);

            item.fromGateId = r.get<db_id>(7);
            item.fromGateLetter = sqlite3_column_text(q.stmt(), 8)[0];
            item.fromStationName = r.get<QString>(9);

            item.toGateId = r.get<db_id>(10);
            item.toGateLetter = sqlite3_column_text(q.stmt(), 11)[0];
            item.toStationName = r.get<QString>(12);
            item.reversed = false;

            if(filterFromStationId)
            {
                if(filterFromStationId == item.toStationId)
                {
                    //Always show filter station as 'From'
                    qSwap(item.fromStationId, item.toStationId);
                    qSwap(item.fromGateId, item.toGateId);
                    qSwap(item.fromStationName, item.toStationName);
                    qSwap(item.fromGateLetter, item.toGateLetter);
                    item.reversed = true;
                }
                //item.fromStationName.clear(); //Save some memory???
            }
            i++;
        }
        if(i < BatchSize)
            vec.remove(i, BatchSize - i);
    }


    RailwaySegmentsResultEvent *ev = new RailwaySegmentsResultEvent;
    ev->items = vec;
    ev->firstRow = first;

    qApp->postEvent(this, ev);
}

void RailwaySegmentsModel::handleResult(const QVector<RailwaySegmentsModel::RailwaySegmentItem> &items, int firstRow)
{
    if(firstRow == cacheFirstRow + cache.size())
    {
        qDebug() << "RES: appending First:" << cacheFirstRow;
        cache.append(items);
        if(cache.size() > ItemsPerPage)
        {
            const int extra = cache.size() - ItemsPerPage; //Round up to BatchSize
            const int remainder = extra % BatchSize;
            const int n = remainder ? extra + BatchSize - remainder : extra;
            qDebug() << "RES: removing last" << n;
            cache.remove(0, n);
            cacheFirstRow += n;
        }
    }
    else
    {
        if(firstRow + items.size() == cacheFirstRow)
        {
            qDebug() << "RES: prepending First:" << cacheFirstRow;
            QVector<RailwaySegmentItem> tmp = items;
            tmp.append(cache);
            cache = tmp;
            if(cache.size() > ItemsPerPage)
            {
                const int n = cache.size() - ItemsPerPage;
                cache.remove(ItemsPerPage, n);
                qDebug() << "RES: removing first" << n;
            }
        }
        else
        {
            qDebug() << "RES: replacing";
            cache = items;
        }
        cacheFirstRow = firstRow;
        qDebug() << "NEW First:" << cacheFirstRow;
    }

    firstPendingRow = -BatchSize;

    int lastRow = firstRow + items.count(); //Last row + 1 extra to re-trigger possible next batch
    if(lastRow >= curItemCount)
        lastRow = curItemCount -1; //Ok, there is no extra row so notify just our batch

    if(firstRow > 0)
        firstRow--; //Try notify also the row before because there might be another batch waiting so re-trigger it
    QModelIndex firstIdx = index(firstRow, 0);
    QModelIndex lastIdx = index(lastRow, NCols - 1);
    emit dataChanged(firstIdx, lastIdx);
    emit itemsReady(firstRow, lastRow);

    qDebug() << "TOTAL: From:" << cacheFirstRow << "To:" << cacheFirstRow + cache.size() - 1;
}

db_id RailwaySegmentsModel::getFilterFromStationId() const
{
    return filterFromStationId;
}

void RailwaySegmentsModel::setFilterFromStationId(const db_id &value)
{
    filterFromStationId = value;
    clearCache();
    refreshData();
}