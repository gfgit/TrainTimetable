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

#include "rsplanmodel.h"

#include "utils/jobcategorystrings.h"

#include <sqlite3pp/sqlite3pp.h>
using namespace sqlite3pp;

#include <QBrush>

RsPlanModel::RsPlanModel(sqlite3pp::database &db, QObject *parent) :
    QAbstractTableModel(parent),
    mDb(db)
{
}

QVariant RsPlanModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section)
        {
        case JobName:
            return tr("Job");
        case Station:
            return tr("Station");
        case Arrival:
            return tr("Arrival");
        case Departure:
            return tr("Departure");
        case Operation:
            return tr("Operation");
        default:
            break;
        }
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

int RsPlanModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_data.size();
}

int RsPlanModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : NCols;
}

QVariant RsPlanModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_data.size() || idx.column() >= NCols)
        return QVariant();

    const RsPlanItem &item     = m_data.at(idx.row());
    const RsPlanItem *prevItem = nullptr;
    if (idx.row() > 0)
        prevItem = &m_data.at(idx.row() - 1);

    switch (role)
    {
    case Qt::DisplayRole:
    {
        switch (idx.column())
        {
        case JobName:
            return JobCategoryName::jobName(item.jobId, item.jobCat);
        case Station:
            return item.stationName;
        case Arrival:
            return item.arrival;
        case Departure:
            return item.departure;
        case Operation:
            return item.op == RsOp::Coupled ? tr("Coupled") : tr("Uncoupled");
        }
        break;
    }
    case Qt::BackgroundRole:
    {
        // Mark cells with errors
        switch (idx.column())
        {
        case Station:
        {
            // Coupled in a different station than previous, this means it gets teleported
            if (item.op == RsOp::Coupled && prevItem && prevItem->stationId != item.stationId)
                return QBrush(QColor(255, 0, 183)); // Magenta
            break;
        }
        case Operation:
        {
            // Coupled while already coupled or uncoupled while not coupled
            if (prevItem && item.op == prevItem->op)
                return QBrush(QColor(255, 110, 110)); // Light red
            break;
        }
        }
        break;
    }
    }

    return QVariant();
}

void RsPlanModel::loadPlan(db_id rsId)
{
    beginResetModel();

    m_data.clear();

    // TODO: load in thread with same query prepared form many models
    query q_selectOps(mDb, "SELECT stops.id,"
                           "stops.job_id,"
                           "jobs.category,"
                           "stops.station_id,"
                           "stops.arrival,"
                           "stops.departure,"
                           "coupling.operation,"
                           "stations.name"
                           " FROM stops"
                           " JOIN coupling ON coupling.stop_id=stops.id AND coupling.rs_id=?"
                           " JOIN jobs ON jobs.id=stops.job_id"
                           " JOIN stations ON stations.id=stops.station_id"
                           " ORDER BY stops.arrival");

    q_selectOps.bind(1, rsId);
    for (auto r : q_selectOps)
    {
        RsPlanItem item;
        item.stopId      = r.get<db_id>(0);
        item.jobId       = r.get<db_id>(1);
        item.jobCat      = JobCategory(r.get<int>(2));
        item.stationId   = r.get<db_id>(3);
        item.arrival     = r.get<QTime>(4);
        item.departure   = r.get<QTime>(5);
        item.op          = RsOp(r.get<int>(6));
        item.stationName = r.get<QString>(7);

        m_data.append(item);
    }

    m_data.squeeze();

    endResetModel();
}

void RsPlanModel::clear()
{
    beginResetModel();
    m_data.clear();
    m_data.squeeze();
    endResetModel();
}

RsPlanItem RsPlanModel::getItem(int row)
{
    return m_data.value(row, RsPlanItem());
}
