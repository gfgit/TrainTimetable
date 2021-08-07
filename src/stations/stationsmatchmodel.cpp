#include "stationsmatchmodel.h"

StationsMatchModel::StationsMatchModel(database &db, QObject *parent) :
    ISqlFKMatchModel(parent),
    mDb(db),
    q_getMatches(mDb),
    m_lineId(0),
    m_exceptStId(0)
{

}

QVariant StationsMatchModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= size)
        return QVariant();

    switch (role)
    {
    case Qt::DisplayRole:
    {
        if(isEmptyRow(idx.row()))
        {
            return ISqlFKMatchModel::tr("Empty");
        }
        else if(isEllipsesRow(idx.row()))
        {
            return ellipsesString;
        }

        return items[idx.row()].name;
    }
    case Qt::FontRole:
    {
        if(isEmptyRow(idx.row()))
        {
            return boldFont();
        }
        break;
    }
    }

    return QVariant();
}

void StationsMatchModel::autoSuggest(const QString &text)
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

void StationsMatchModel::refreshData()
{
    if(!mDb.db())
        return;

    beginResetModel();

    char emptyQuery = '%';

    if(mQuery.isEmpty())
        sqlite3_bind_text(q_getMatches.stmt(), 1, &emptyQuery, 1, SQLITE_STATIC);
    else
        sqlite3_bind_text(q_getMatches.stmt(), 1, mQuery, mQuery.size(), SQLITE_STATIC);

    if(m_exceptStId)
        q_getMatches.bind(2, m_exceptStId);
    if(m_lineId)
        q_getMatches.bind(3, m_lineId);

    auto end = q_getMatches.end();
    auto it = q_getMatches.begin();
    int i = 0;
    for(; i < MaxMatchItems && it != end; i++)
    {
        items[i].stationId = (*it).get<db_id>(0);
        items[i].name = (*it).get<QString>(1);
        ++it;
    }

    size = i + 1; //Items + Empty

    if(it != end)
    {
        //There would be still rows, show Ellipses
        size++; //Items + Empty + Ellispses
    }

    q_getMatches.reset();
    endResetModel();

    emit resultsReady(false);
}

QString StationsMatchModel::getName(db_id id) const
{
    if(!mDb.db())
        return QString();

    query q(mDb, "SELECT name FROM stations WHERE id=?");
    q.bind(1, id);
    if(q.step() == SQLITE_ROW)
        return q.getRows().get<QString>(0);
    return QString();
}

db_id StationsMatchModel::getIdAtRow(int row) const
{
    return items[row].stationId;
}

QString StationsMatchModel::getNameAtRow(int row) const
{
    return items[row].name;
}

void StationsMatchModel::setFilter(db_id lineId, db_id exceptStId)
{
    m_lineId = lineId;
    m_exceptStId = exceptStId;

    QByteArray sql;

    if(m_lineId)
    {
        sql = "SELECT railways.stationId,"
              " stations.name"
              " FROM railways"
              " JOIN stations ON stations.id=railways.stationId"
              " WHERE railways.lineId=?3 AND ";
        if(m_exceptStId)
            sql.append("railways.stationId<>?2 AND ");
    }else{
        sql = "SELECT stations.id,stations.name FROM stations WHERE ";
        if(m_exceptStId)
            sql.append("stations.id<>?2 AND ");
    }

    sql.append("(stations.name LIKE ?1 OR stations.short_name LIKE ?1) LIMIT " QT_STRINGIFY(MaxMatchItems + 1));

    q_getMatches.prepare(sql.constData());
}