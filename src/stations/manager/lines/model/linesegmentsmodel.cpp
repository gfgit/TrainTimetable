#include "linesegmentsmodel.h"

#include <QCoreApplication>
#include <QEvent>
#include "utils/worker_event_types.h"

#include <sqlite3pp/sqlite3pp.h>
using namespace sqlite3pp;

#include <QColor>
#include <QFont>

#include "utils/kmutils.h"

#include <QDebug>

class LineSegmentsModelResultEvent : public QEvent
{
public:
    static constexpr Type _Type = Type(CustomEvents::LineSegmentsModelResult);
    inline LineSegmentsModelResultEvent() : QEvent(_Type) {}

    QVector<LineSegmentsModel::LineSegmentItem> items;
};

LineSegmentsModel::LineSegmentsModel(sqlite3pp::database &db, QObject *parent) :
    IPagedItemModel(BatchSize, db, parent),
    m_lineId(0),
    isFetching(false)
{
    sortColumn = SegmentPosCol;
}

bool LineSegmentsModel::event(QEvent *e)
{
    if(e->type() == LineSegmentsModelResultEvent::_Type)
    {
        LineSegmentsModelResultEvent *ev = static_cast<LineSegmentsModelResultEvent *>(e);
        ev->setAccepted(true);

        segments = ev->items;
        isFetching = false; //Done fetching

        QModelIndex firstIdx = index(0, 0);
        QModelIndex lastIdx = index(totalItemsCount - 1, NCols - 1);
        emit dataChanged(firstIdx, lastIdx);
        emit itemsReady(0, totalItemsCount - 1);

        return true;
    }

    return QAbstractTableModel::event(e);
}

QVariant LineSegmentsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        {
            switch (section)
            {
            case StationOrSegmentNameCol:
                return tr("Station");
            case KmPosCol:
                return tr("Km");
            case MaxSpeedCol:
                return tr("Max. Speed");
            case DistanceCol:
                return tr("Distance");
            }
            break;
        }
        }
    }
    else if(role == Qt::DisplayRole)
    {
        //Vertical row numbers

        //Station rows: show station index position (start from 1)
        if(getRowType(section) == StationRow)
            return section / 2 + 1;

        return QVariant(); //Hide number on segment rows
    }

    return QAbstractTableModel::headerData(section, orientation, role);
}

int LineSegmentsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : totalItemsCount;
}

int LineSegmentsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : NCols;
}

QVariant LineSegmentsModel::data(const QModelIndex &idx, int role) const
{
    const int segmentIdx = getItemIndex(idx.row());

    if (!idx.isValid() || idx.row() >= totalItemsCount || idx.column() >= NCols)
        return QVariant();

    if(segments.isEmpty())
    {
        const_cast<LineSegmentsModel *>(this)->fetchRows();
        return QVariant();
    }

    const LineSegmentItem& item = segments.at(segmentIdx);

    switch (getRowType(idx.row()))
    {
    case StationRow:
    {
        switch (role)
        {
        case Qt::DisplayRole:
        {
            switch (idx.column())
            {
            case StationOrSegmentNameCol:
                return item.fromStationName;
            case KmPosCol:
                return utils::kmNumToText(item.fromPosMeters);
            }
            break;
        }
        case Qt::FontRole:
        {
            //Station names in bold
            switch (idx.column())
            {
            case StationOrSegmentNameCol:
            {
                QFont f;
                f.setBold(true);
                return f;
            }
            }
            break;
        }
        case Qt::TextAlignmentRole:
        {
            switch (idx.column())
            {
            case KmPosCol:
                return Qt::AlignRight + Qt::AlignVCenter;
            }
            break;
        }
        }
        break;
    }
    case SegmentRow:
    {
        switch (role)
        {
        case Qt::DisplayRole:
        {
            switch (idx.column())
            {
            case StationOrSegmentNameCol:
                return item.segmentName;
            case MaxSpeedCol:
                return QStringLiteral("%1 km/h").arg(item.maxSpeedKmH);
            case DistanceCol:
                return utils::kmNumToText(item.distanceMeters);
            }
            break;
        }
        case Qt::DecorationRole:
        {
            //Draw a small blue square to distinguish it's a segment
            switch (idx.column())
            {
            case StationOrSegmentNameCol:
                return item.reversed ? QColor(Qt::red) : QColor(Qt::blue);
            }
            break;
        }
        case Qt::ToolTipRole:
        {
            switch (idx.column())
            {
            case StationOrSegmentNameCol:
            {
                if(item.reversed)
                    return tr("Segment <b>%1</b> is reversed.").arg(item.segmentName);
            }
            }
            break;
        }
        case Qt::TextAlignmentRole:
        {
            //Align segment names to right
            switch (idx.column())
            {
            case StationOrSegmentNameCol:
            case MaxSpeedCol:
            case DistanceCol:
                return Qt::AlignRight + Qt::AlignVCenter;
            }
            break;
        }
        }
        break;
    }
    }

    return QVariant();
}

void LineSegmentsModel::clearCache()
{
    segments.clear();
    segments.squeeze();
}

void LineSegmentsModel::refreshData()
{
    if(!mDb.db())
        return;

    emit itemsReady(-1, -1); //Notify we are about to refresh

    //TODO: consider filters
    query q(mDb, "SELECT COUNT(1) FROM line_segments WHERE line_id=?");
    q.bind(1, m_lineId);
    q.step();

    //Store segment count
    curItemCount = q.getRows().get<int>(0);

    //Each segment has 2 row + last station row
    const int actualRowCount = curItemCount * 2 + 1;

    if(actualRowCount != totalItemsCount)
    {
        beginResetModel();

        clearCache();
        totalItemsCount = actualRowCount;

        //Always 1 page
        pageCount = 1;

        emit totalItemsCountChanged(totalItemsCount);
        emit pageCountChanged(pageCount);

        endResetModel();
    }
}

void LineSegmentsModel::setSortingColumn(int col)
{
    //Sort only by segment index position
    Q_UNUSED(col)
}

void LineSegmentsModel::setLine(db_id lineId)
{
    m_lineId = lineId;
    clearCache();
    refreshData();
}

bool LineSegmentsModel::getLineInfo(QString &nameOut, int &startMetersOut) const
{
    query q(mDb, "SELECT name, start_meters FROM lines WHERE id=?");
    q.bind(1, m_lineId);
    if(q.step() != SQLITE_ROW)
        return false;

    nameOut = q.getRows().get<QString>(0);
    startMetersOut = q.getRows().get<int>(1);
    return true;
}

bool LineSegmentsModel::setLineInfo(const QString &name, int startMeters)
{
    command cmd(mDb, "UPDATE lines SET name=?, start_meters=?  WHERE id=?");
    cmd.bind(1, name);
    cmd.bind(2, startMeters);
    cmd.bind(3, m_lineId);
    if(cmd.execute() != SQLITE_OK)
    {
        emit modelError(mDb.error_msg());
        return false;
    }

    //Update model
    refreshData();
    return true;
}

void LineSegmentsModel::fetchRows()
{
    if(!mDb.db())
        return;

    //Prevent multiple requests
    if(isFetching)
        return;

    isFetching = true;

    //FIXME: consider also segment and station types
    //and gate names and rail track connections count

    query q(mDb, "SELECT start_meters FROM lines WHERE id=?");
    q.bind(1, m_lineId);
    if(q.step() != SQLITE_ROW)
    {
        qWarning() << "INVALID LINE:" << m_lineId;
    }

    //First station of the line has this position
    int currentPosMeters = q.getRows().get<int>(0);

    q.prepare("SELECT ls.id, ls.seg_id, ls.direction,"
              "seg.name, seg.max_speed_kmh, seg.distance_meters,"
              "s1.id, s1.name, s2.id, s2.name"
              " FROM line_segments ls"
              " JOIN railway_segments seg ON seg.id=ls.seg_id"
              " JOIN station_gates g1 ON g1.id=seg.in_gate_id"
              " JOIN station_gates g2 ON g2.id=seg.out_gate_id"
              " JOIN stations s1 ON s1.id=g1.station_id"
              " JOIN stations s2 ON s2.id=g2.station_id"
              " WHERE ls.line_id=?"
              " ORDER BY ls.pos");

    q.bind(1, m_lineId);

    //Reverse for 1 extra item (which will hold last station)

    QVector<LineSegmentItem> vec;
    vec.reserve(curItemCount + 1);

    db_id lastStationId = 0;
    QString lastStationName;

    for(auto seg : q)
    {
        LineSegmentItem item;

        item.lineSegmentId = seg.get<db_id>(0);
        item.railwaySegmentId = seg.get<db_id>(1);
        item.reversed = seg.get<int>(2) != 0;

        item.segmentName = seg.get<QString>(3);
        item.maxSpeedKmH = seg.get<int>(4);
        item.distanceMeters = seg.get<int>(5);

        //Store first segment end
        item.fromStationId = seg.get<db_id>(6);
        item.fromStationName = seg.get<QString>(7);

        //Store also the other end of segment for last item
        db_id otherStationId = seg.get<db_id>(8);
        QString otherStationName = seg.get<QString>(9);

        if(item.reversed)
        {
            //Swap segments ends
            qSwap(item.fromStationId, otherStationId);
            qSwap(item.fromStationName, otherStationName);
        }

        if(lastStationId && item.fromStationId != lastStationId)
        {
            qWarning() << "Line segments are not adjacent, ID:" << item.lineSegmentId
                       << "LINE:" << m_lineId;
        }

        lastStationId = otherStationId;
        lastStationName = otherStationName;

        item.fromPosMeters = currentPosMeters;
        currentPosMeters += item.distanceMeters;

        vec.append(item);
    }

    //Add a fake item to show last station (other end of last segment)
    //This item is shown without a SegmentRow
    LineSegmentItem lastItem;
    lastItem.fromStationId = lastStationId;
    lastItem.fromStationName = lastStationName;
    lastItem.fromPosMeters = currentPosMeters;
    //Fields relevant only for SegmentRow so we don't use them
    lastItem.lineSegmentId = 0;
    lastItem.railwaySegmentId = 0;
    lastItem.distanceMeters = 0;
    lastItem.maxSpeedKmH = 0;
    lastItem.reversed = false;
    vec.append(lastItem);

    //NOTE: Send items in queued event
    //We are called from inside data()
    //If we update directly the rowCount() will change
    //while the view is traversing the model -> BAD
    //By sending in queue we ensure no view is currently traversing the model

    LineSegmentsModelResultEvent *ev = new LineSegmentsModelResultEvent;
    ev->items = vec;

    qApp->postEvent(this, ev);
}