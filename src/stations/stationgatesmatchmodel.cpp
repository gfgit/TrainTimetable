#include "stationgatesmatchmodel.h"

#include "station_name_utils.h"

#include <QBrush>

using namespace sqlite3pp;

StationGatesMatchModel::StationGatesMatchModel(sqlite3pp::database &db, QObject *parent) :
    ISqlFKMatchModel(parent),
    mDb(db),
    q_getMatches(mDb, "SELECT id,type,name,side"
                      " FROM station_gates WHERE station_id=?2 AND name LIKE ?1"
                      " ORDER BY side,name"),
    m_stationId(0),
    hideConnectedGates(false)
{
}

QVariant StationGatesMatchModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= size)
        return QVariant();

    const bool emptyRow = hasEmptyRow &&
                          (idx.row() == ItemCount || (size < ItemCount + 2 && idx.row() == size - 1));
    const bool ellipsesRow = idx.row() == (ItemCount - 1 - (hasEmptyRow ? 0 : 1));

    switch (role)
    {
    case Qt::DisplayRole:
    {
        if(emptyRow)
        {
            return ISqlFKMatchModel::tr("Empty");
        }
        else if(ellipsesRow)
        {
            return ellipsesString;
        }

        return items[idx.row()].name;
    }
    case Qt::ToolTipRole:
    {
        if(!emptyRow && !ellipsesRow)
        {
            return tr("Gate <b>%1</b> is %2.")
                .arg(items[idx.row()].name,
                     utils::StationUtils::name(items[idx.row()].side));
        }
        break;
    }
    case Qt::FontRole:
    {
        if(emptyRow)
        {
            return boldFont();
        }
        break;
    }
    case Qt::TextAlignmentRole:
    {
        if(!emptyRow && !ellipsesRow)
            return Qt::AlignRight + Qt::AlignVCenter;
        break;
    }
    case Qt::BackgroundRole:
    {
        if(!emptyRow && !ellipsesRow)
        {
            QColor color;
            if(items[idx.row()].type.testFlag(utils::GateType::Bidirectional))
                break; //Default color

            if(items[idx.row()].type.testFlag(utils::GateType::Entrance))
                color = Qt::green; //Entrance only
            else if(items[idx.row()].type.testFlag(utils::GateType::Exit))
                color = Qt::red; //Exit only
            color.setAlpha(80);
            return QBrush(color);
        }
        break;
    }
    case Qt::DecorationRole:
    {
        if(!emptyRow && !ellipsesRow)
        {
            if(items[idx.row()].side == utils::Side::West)
                return QColor(Qt::blue);
        }
        break;
    }
    }

    return QVariant();
}

void StationGatesMatchModel::autoSuggest(const QString &text)
{
    mQuery.clear();
    if(!text.isEmpty())
    {
        mQuery.clear();
        mQuery.reserve(text.size() + 2);
        mQuery.append('%');
        mQuery.append(text.toUtf8());
        mQuery.append('%');
    }

    refreshData();
}

void StationGatesMatchModel::refreshData()
{
    if(!mDb.db())
        return;

    beginResetModel();

    char emptyQuery = '%';

    if(mQuery.isEmpty())
        sqlite3_bind_text(q_getMatches.stmt(), 1, &emptyQuery, 1, SQLITE_STATIC);
    else
        sqlite3_bind_text(q_getMatches.stmt(), 1, mQuery, mQuery.size(), SQLITE_STATIC);

    q_getMatches.bind(2, m_stationId);

    auto end = q_getMatches.end();
    auto it = q_getMatches.begin();
    int i = 0;
    for(; i < ItemCount && it != end; i++)
    {
        auto track = *it;
        items[i].gateId = track.get<db_id>(0);
        items[i].type = utils::GateType(track.get<int>(1));
        items[i].name = track.get<QString>(2);
        items[i].side = utils::Side(track.get<int>(3));
        ++it;
    }

    size = i;
    if(hasEmptyRow)
        size++; //Items + Empty

    if(it != end)
    {
        //There would be still rows, show Ellipses
        size++; //Items + Empty + Ellispses
    }

    q_getMatches.reset();
    endResetModel();

    emit resultsReady(false);
}

QString StationGatesMatchModel::getName(db_id id) const
{
    if(!mDb.db())
        return QString();

    query q(mDb, "SELECT name FROM station_gates WHERE id=?");
    q.bind(1, id);
    if(q.step() == SQLITE_ROW)
        return q.getRows().get<QString>(0);
    return QString();
}

db_id StationGatesMatchModel::getIdAtRow(int row) const
{
    return items[row].gateId;
}

QString StationGatesMatchModel::getNameAtRow(int row) const
{
    return items[row].name;
}

void StationGatesMatchModel::setFilter(db_id stationId, bool hideAlreadyConnected)
{
    m_stationId = stationId;
    hideConnectedGates = hideAlreadyConnected;

    QByteArray sql = "SELECT g.id,g.type,g.name,g.side"
                     " FROM station_gates g WHERE g.station_id=?2 AND g.name LIKE ?1";

    if(hideAlreadyConnected)
    {
        sql += " AND NOT EXISTS (SELECT 1 FROM railway_segments s WHERE s.in_gate_id=g.id OR s.out_gate_id=g.id)";
    }

    sql += " ORDER BY g.side,g.name";

    q_getMatches.prepare(sql);

    refreshData();
}

StationGatesMatchFactory::StationGatesMatchFactory(database &db, QObject *parent) :
    IMatchModelFactory(parent),
    m_stationId(0),
    hideConnectedGates(false),
    mDb(db)
{

}

ISqlFKMatchModel *StationGatesMatchFactory::createModel()
{
    StationGatesMatchModel *m = new StationGatesMatchModel(mDb);
    m->setFilter(m_stationId, hideConnectedGates);
    return m;
}
