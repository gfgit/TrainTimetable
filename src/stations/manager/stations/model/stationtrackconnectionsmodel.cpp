/*
 * ModelRailroadTimetablePlanner
 * Copyright 2016-2023, Filippo Gentile
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stationtrackconnectionsmodel.h"

#include "utils/delegates/sql/pageditemmodelhelper_impl.h"

#include <sqlite3pp/sqlite3pp.h>
using namespace sqlite3pp;

#include "stations/station_name_utils.h"

#include <QDebug>

// Error messages

static constexpr char errorTrackConnInUseText[] = QT_TRANSLATE_NOOP(
  "StationTrackConnectionsModel",
  "This track connection is still referenced.\n"
  "Please change remove or change path to all Jobs travelling on this connection.");

static constexpr char errorConnAlreadyExistsText[] =
  QT_TRANSLATE_NOOP("StationTrackConnectionsModel",
                    "This track is already connected to this gade track by this side.");

static constexpr char errorInvalidGateId[] =
  QT_TRANSLATE_NOOP("StationTrackConnectionsModel", "Please select a valid Gate.");

static constexpr char errorGateOnDifferentStation[] = QT_TRANSLATE_NOOP(
  "StationTrackConnectionsModel", "The selected Gate belongs to a different station.");

static constexpr char errorGateTrackOutOfBound[] =
  QT_TRANSLATE_NOOP("StationTrackConnectionsModel", "Gate track is out of bound.<br>"
                                                    "The selected Gate has only <b>%1</b> tracks.");

StationTrackConnectionsModel::StationTrackConnectionsModel(sqlite3pp::database &db,
                                                           QObject *parent) :
    BaseClass(500, db, parent),
    m_stationId(0),
    editable(false)
{
    sortColumn = TrackCol;
}

QVariant StationTrackConnectionsModel::headerData(int section, Qt::Orientation orientation,
                                                  int role) const
{
    if (orientation == Qt::Horizontal)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        {
            switch (section)
            {
            case TrackCol:
                return tr("Track");
            case TrackSideCol:
                return tr("Side");
            case GateCol:
                return tr("Gate");
            case GateTrackCol:
                return tr("Gate Track");
            }
            break;
        }
        }
    }
    else if (role == Qt::DisplayRole)
    {
        return section + curPage * ItemsPerPage + 1;
    }

    return QAbstractTableModel::headerData(section, orientation, role);
}

QVariant StationTrackConnectionsModel::data(const QModelIndex &idx, int role) const
{
    const int row = idx.row();
    if (!idx.isValid() || row >= curItemCount || idx.column() >= NCols)
        return QVariant();

    if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
    {
        // Fetch above or below current cache
        const_cast<StationTrackConnectionsModel *>(this)->fetchRow(row);

        // Temporarily return null
        return role == Qt::DisplayRole ? QVariant("...") : QVariant();
    }

    const TrackConnItem &item = cache.at(row - cacheFirstRow);

    switch (role)
    {
    case Qt::DisplayRole:
    {
        switch (idx.column())
        {
        case TrackCol:
            return item.trackName;
        case TrackSideCol:
            return utils::StationUtils::name(item.trackSide);
        case GateCol:
            return item.gateName;
        case GateTrackCol:
            return item.gateTrack;
        }
        break;
    }
    case Qt::EditRole:
    {
        switch (idx.column())
        {
        case TrackSideCol:
            return int(item.trackSide);
        case GateTrackCol:
            return item.gateTrack;
        }
        break;
    }
    case Qt::TextAlignmentRole:
    {
        switch (idx.column())
        {
        case TrackCol:
        case GateTrackCol:
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        case GateCol:
            return Qt::AlignCenter;
        }
        break;
    }
    }

    return QVariant();
}

bool StationTrackConnectionsModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    if (!editable)
        return false;

    const int row = idx.row();
    if (!idx.isValid() || row >= curItemCount || idx.column() >= NCols || row < cacheFirstRow
        || row >= cacheFirstRow + cache.size())
        return false; // Not fetched yet or invalid

    TrackConnItem &item = cache[row - cacheFirstRow];
    QModelIndex first   = idx;
    QModelIndex last    = idx;

    switch (role)
    {
    case Qt::EditRole:
    {
        switch (idx.column())
        {
        case TrackSideCol:
        {
            bool ok = false;
            int val = value.toInt(&ok);
            if (!ok || !setTrackSide(item, val))
                return false;
            break;
        }
        case GateTrackCol:
        {
            bool ok = false;
            int val = value.toInt(&ok);
            if (!ok || !setGateTrack(item, val))
                return false;
            break;
        }
        }
        break;
    }
    }

    emit dataChanged(first, last);

    return true;
}

Qt::ItemFlags StationTrackConnectionsModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
    if (!editable || idx.row() < cacheFirstRow || idx.row() >= cacheFirstRow + cache.size())
        return f; // Not fetched yet

    f.setFlag(Qt::ItemIsEditable);
    return f;
}

void StationTrackConnectionsModel::setSortingColumn(int col)
{
    if (sortColumn == col || col >= NCols)
        return;

    clearCache();
    sortColumn        = col;

    QModelIndex first = index(0, 0);
    QModelIndex last  = index(curItemCount - 1, NCols - 1);
    emit dataChanged(first, last);
}

bool StationTrackConnectionsModel::getFieldData(int row, int col, db_id &idOut,
                                                QString &nameOut) const
{
    if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
        return false;

    const TrackConnItem &item = cache[row - cacheFirstRow];

    switch (col)
    {
    case TrackCol:
    {
        idOut   = item.trackId;
        nameOut = item.trackName;
        break;
    }
    case GateCol:
    {
        idOut   = item.gateId;
        nameOut = item.gateName;
        break;
    }
    default:
        return false;
    }

    return true;
}

bool StationTrackConnectionsModel::validateData(int row, int col, db_id id, const QString &name)
{
    Q_UNUSED(row)
    Q_UNUSED(col)
    Q_UNUSED(id)
    Q_UNUSED(name)
    return true;
}

bool StationTrackConnectionsModel::setFieldData(int row, int col, db_id id, const QString &name)
{
    if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
        return false;

    TrackConnItem &item = cache[row - cacheFirstRow];
    bool ret            = false;
    switch (col)
    {
    case TrackCol:
    {
        ret = setTrack(item, id, name);
        break;
    }
    case GateCol:
    {
        ret = setGate(item, id, name);
        break;
    }
    }

    if (ret)
    {
        QModelIndex idx = index(row, col);
        emit dataChanged(idx, idx);
    }

    return ret;
}

bool StationTrackConnectionsModel::setStation(db_id stationId)
{
    m_stationId = stationId;
    refreshData(true);
    return true;
}

bool StationTrackConnectionsModel::addTrackConnection(db_id trackId, utils::Side trackSide,
                                                      db_id gateId, int gateTrack)
{
    if (!trackId || !gateId || gateTrack < 0)
        return false;

    command q_newTrackConn(
      mDb, "INSERT INTO station_gate_connections(id, track_id, track_side, gate_id, gate_track)"
           " VALUES(NULL,?,?,?,?)");
    q_newTrackConn.bind(1, trackId);
    q_newTrackConn.bind(2, int(trackSide));
    q_newTrackConn.bind(3, gateId);
    q_newTrackConn.bind(4, gateTrack);

    sqlite3_mutex *mutex = sqlite3_db_mutex(mDb.db());
    sqlite3_mutex_enter(mutex);
    int ret      = q_newTrackConn.execute();
    db_id connId = mDb.last_insert_rowid();
    sqlite3_mutex_leave(mutex);
    q_newTrackConn.reset();

    if ((ret != SQLITE_OK && ret != SQLITE_DONE) || connId == 0)
    {
        // Error

        if (ret == SQLITE_CONSTRAINT_UNIQUE)
        {
            emit modelError(tr(errorConnAlreadyExistsText));
        }
        else
        {
            emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        }
        return false;
    }

    refreshData();   // Recalc row count
    switchToPage(0); // Reset to first page and so it is shown as first row

    return true;
}

bool StationTrackConnectionsModel::removeTrackConnection(db_id connId)
{
    command q(mDb, "DELETE FROM station_gate_connections WHERE id=?");

    q.bind(1, connId);
    int ret = q.execute();
    q.reset();

    if (ret != SQLITE_OK)
    {
        if (ret == SQLITE_CONSTRAINT_FOREIGNKEY || ret == SQLITE_CONSTRAINT_TRIGGER)
        {
            // TODO: show more information to the user, like where it's still referenced
            emit modelError(tr(errorTrackConnInUseText));
        }
        else
        {
            emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        }

        return false;
    }

    refreshData(); // Recalc row count

    emit trackConnRemoved(connId, 0, 0);

    return true;
}

bool StationTrackConnectionsModel::addTrackToAllGatesOnSide(db_id trackId, utils::Side side,
                                                            int preferredGateTrack)
{
    if (!trackId)
        return false;

    if (preferredGateTrack < 0)
        preferredGateTrack = 0;

    command q_newTrackConn(
      mDb, "INSERT INTO station_gate_connections(id, track_id, track_side, gate_id, gate_track)"
           " VALUES(NULL,?,?,?,?)");

    // Select all gates on requested side
    query q_selectGates(
      mDb, "SELECT id,out_track_count FROM station_gates WHERE station_id=? AND side=?");
    q_selectGates.bind(1, m_stationId);
    q_selectGates.bind(2, int(side));

    for (auto gate : q_selectGates)
    {
        db_id gateId      = gate.get<db_id>(0);
        int outTrackCount = gate.get<int>(1);

        // If gate track is out of bound chose the highest
        int gateTrack = preferredGateTrack;
        if (gateTrack >= outTrackCount)
            gateTrack = outTrackCount - 1;

        // Ignore return codes
        // Some connections may already exist
        // Skip them by ignoring failed inserts
        q_newTrackConn.bind(1, trackId);
        q_newTrackConn.bind(2, int(side));
        q_newTrackConn.bind(3, gateId);
        q_newTrackConn.bind(4, gateTrack);
        q_newTrackConn.execute();
        q_newTrackConn.reset();
    }

    refreshData();   // Recalc row count
    switchToPage(0); // Reset to first page and so it is shown as first row

    return true;
}

bool StationTrackConnectionsModel::addGateToAllTracks(db_id gateId, int gateTrack)
{
    if (!gateId)
        return false;

    if (gateTrack < 0)
        gateTrack = 0;

    command q_newTrackConn(
      mDb, "INSERT INTO station_gate_connections(id, track_id, track_side, gate_id, gate_track)"
           " VALUES(NULL,?,?,?,?)");

    // Select gate side and track count
    query q(mDb, "SELECT station_id,out_track_count,side FROM station_gates WHERE id=?");
    q.bind(1, gateId);
    int ret = q.step();
    if (ret != SQLITE_ROW)
    {
        // Invalid Id
        emit modelError(tr(errorInvalidGateId));
        return false;
    }

    db_id gateStationId = q.getRows().get<db_id>(0);
    if (gateStationId != m_stationId)
    {
        // Gate belongs to a different station.
        emit modelError(tr(errorGateOnDifferentStation));
        return false;
    }

    int outTrackCount     = q.getRows().get<int>(1);
    utils::Side trackSide = utils::Side(q.getRows().get<int>(2));

    if (gateTrack >= outTrackCount)
    {
        // Gate track out of bound
        emit modelError(tr(errorGateTrackOutOfBound).arg(outTrackCount));
        return false;
    }

    // Select all tracks
    q.prepare("SELECT id FROM station_tracks WHERE station_id=?");
    q.bind(1, m_stationId);

    for (auto track : q)
    {
        db_id trackId = track.get<db_id>(0);

        // Ignore return codes
        // Some connections may already exist
        // Skip them by ignoring failed inserts
        q_newTrackConn.bind(1, trackId);
        q_newTrackConn.bind(2, int(trackSide));
        q_newTrackConn.bind(3, gateId);
        q_newTrackConn.bind(4, gateTrack);
        q_newTrackConn.execute();
        q_newTrackConn.reset();
    }

    refreshData();   // Recalc row count
    switchToPage(0); // Reset to first page and so it is shown as first row

    return true;
}

qint64 StationTrackConnectionsModel::recalcTotalItemCount()
{
    // TODO: consider filters
    query q(mDb, "SELECT COUNT(1) FROM station_gate_connections c"
                 " JOIN station_gates g ON g.id=c.gate_id WHERE g.station_id=?");
    q.bind(1, m_stationId);
    q.step();
    const qint64 count = q.getRows().get<qint64>(0);
    return count;
}

void StationTrackConnectionsModel::internalFetch(int first, int sortCol, int valRow,
                                                 const QVariant &val)
{
    query q(mDb);

    int offset   = first - valRow + curPage * ItemsPerPage;
    bool reverse = false;

    if (valRow > first)
    {
        offset  = 0;
        reverse = true;
    }

    qDebug() << "Fetching:" << first << "ValRow:" << valRow << val << "Offset:" << offset
             << "Reverse:" << reverse;

    const char *whereCol = nullptr;

    QByteArray sql       = "SELECT c.id, c.track_id, c.track_side, t.name,"
                           "c.gate_id, g.name, c.gate_track FROM station_gate_connections c"
                           " JOIN station_tracks t ON t.id=c.track_id"
                           " JOIN station_gates g ON g.id=c.gate_id";
    switch (sortCol)
    {
    case TrackCol:
        whereCol = "t.pos,c.track_side,g.name,c.gate_track";
        break;
    case TrackSideCol:
        whereCol = "c.track_side,t.pos,g.name,c.gate_track";
        break;
    case GateCol:
        whereCol = "g.name,t.pos,c.track_side,c.gate_track";
        break;
    case GateTrackCol:
        whereCol = "g.name,c.gate_track,t.pos,c.track_side";
        break;
    }

    //    if(val.isValid())
    //    {
    //        sql += " WHERE ";
    //        sql += whereCol;
    //        if(reverse)
    //            sql += "<?3";
    //        else
    //            sql += ">?3";
    //    }
    sql += " WHERE g.station_id=?4";

    sql += " ORDER BY ";
    sql += whereCol;

    if (reverse)
        sql += " DESC";

    sql += " LIMIT ?1";
    if (offset)
        sql += " OFFSET ?2";

    q.prepare(sql);
    q.bind(1, BatchSize);
    if (offset)
        q.bind(2, offset);

    q.bind(4, m_stationId);

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

    QList<TrackConnItem> vec(BatchSize);

    auto it             = q.begin();
    const auto end      = q.end();

    int i               = reverse ? BatchSize - 1 : 0;
    const int increment = reverse ? -1 : 1;

    for (; it != end; ++it)
    {
        auto r              = *it;
        TrackConnItem &item = vec[i];
        item.connId         = r.get<db_id>(0);
        item.trackId        = r.get<db_id>(1);
        item.trackSide      = utils::Side(r.get<int>(2));
        item.trackName      = r.get<QString>(3);
        item.gateId         = r.get<db_id>(4);
        item.gateName       = r.get<QString>(5);
        item.gateTrack      = r.get<int>(6);

        i += increment;
    }

    if (reverse && i > -1)
        vec.remove(0, i + 1);
    else if (i < BatchSize)
        vec.remove(i, BatchSize - i);

    postResult(vec, first);
}

bool StationTrackConnectionsModel::setTrackSide(StationTrackConnectionsModel::TrackConnItem &item,
                                                int val)
{
    utils::Side side = utils::Side(val);
    if (item.trackSide == side)
        return false;

    command q(mDb, "UPDATE station_gate_connections SET track_side=? WHERE id=?");
    q.bind(1, val);
    q.bind(2, item.connId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        return false;
    }

    item.trackSide = side;

    // This row has now changed position so we need to invalidate cache
    // HACK: we emit dataChanged for this index (that doesn't exist anymore)
    // but the view will trigger fetching at same scroll position so it is enough
    cache.clear();
    cacheFirstRow = 0;

    return true;
}

bool StationTrackConnectionsModel::setGateTrack(StationTrackConnectionsModel::TrackConnItem &item,
                                                int track)
{
    if (item.gateTrack == track)
        return false;

    command q(mDb, "UPDATE station_gate_connections SET gate_track=? WHERE id=?");
    q.bind(1, track);
    q.bind(2, item.connId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        return false;
    }

    item.gateTrack = track;

    // This row has now changed position so we need to invalidate cache
    // HACK: we emit dataChanged for this index (that doesn't exist anymore)
    // but the view will trigger fetching at same scroll position so it is enough
    cache.clear();
    cacheFirstRow = 0;

    return true;
}

bool StationTrackConnectionsModel::setTrack(StationTrackConnectionsModel::TrackConnItem &item,
                                            db_id trackId, const QString &trackName)
{
    if (item.trackId == trackId)
        return false;

    command q(mDb, "UPDATE station_gate_connections SET track_id=? WHERE id=?");
    if (trackId)
        q.bind(1, trackId);
    else
        q.bind(1); // Bind NULL
    q.bind(2, item.connId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        return false;
    }

    // Old track is no more connected by this connection
    emit trackConnRemoved(item.connId, item.trackId, item.gateId);

    item.trackId   = trackId;
    item.trackName = trackName;

    // This row has now changed position so we need to invalidate cache
    // HACK: we emit dataChanged for this index (that doesn't exist anymore)
    // but the view will trigger fetching at same scroll position so it is enough
    cache.clear();
    cacheFirstRow = 0;

    return true;
}

bool StationTrackConnectionsModel::setGate(StationTrackConnectionsModel::TrackConnItem &item,
                                           db_id gateId, const QString &gateName)
{
    if (item.gateId == gateId)
        return false;

    command q(mDb, "UPDATE station_gate_connections SET gate_id=? WHERE id=?");
    if (gateId)
        q.bind(1, gateId);
    else
        q.bind(1); // Bind NULL
    q.bind(2, item.connId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        return false;
    }

    // Old gate is no more connected by this connection
    emit trackConnRemoved(item.connId, item.trackId, item.gateId);

    item.gateId   = gateId;
    item.gateName = gateName;

    // This row has now changed position so we need to invalidate cache
    // HACK: we emit dataChanged for this index (that doesn't exist anymore)
    // but the view will trigger fetching at same scroll position so it is enough
    cache.clear();
    cacheFirstRow = 0;

    return true;
}
