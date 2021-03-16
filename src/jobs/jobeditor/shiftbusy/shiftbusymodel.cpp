#include "shiftbusymodel.h"
#include "utils/model_roles.h"

#include "utils/jobcategorystrings.h"

#include <sqlite3pp/sqlite3pp.h>
using namespace sqlite3pp;

ShiftBusyModel::ShiftBusyModel(sqlite3pp::database &db, QObject *parent) :
    QAbstractTableModel(parent),
    mDb(db),
    m_shiftId(0),
    m_jobId(0),
    m_jobCat(JobCategory::FREIGHT)
{
}

QVariant ShiftBusyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section) {
        case JobCol:
            return tr("Job");
        case Start:
            return tr("From");
        case End:
            return tr("To");
        }
    }

    return QAbstractTableModel::headerData(section, orientation, role);
}

int ShiftBusyModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_data.size();
}

int ShiftBusyModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : NCols;
}

QVariant ShiftBusyModel::data(const QModelIndex &idx, int role) const
{
    const int row = idx.row();
    if (!idx.isValid() || row >= m_data.size() || idx.column() >= NCols)
        return QVariant();

    const JobInfo& info = m_data.at(row);

    switch (role)
    {
    case Qt::DisplayRole:
    {
        switch (idx.column())
        {
        case JobCol:
            return JobCategoryName::jobName(info.jobId, info.jobCat);
        case Start:
            return info.start;
        case End:
            return info.end;
        }
        break;
    }
    case JOB_ID_ROLE:
    {
        return info.jobId;
    }
    }

    return QVariant();
}

void ShiftBusyModel::loadData(db_id shiftId, db_id jobId, const QTime &start, const QTime &end)
{
    beginResetModel();
    m_data.clear();

    m_shiftId = shiftId;
    m_jobId = jobId;
    m_start = start;
    m_end = end;

    query q(mDb, "SELECT name FROM jobshifts WHERE id=?");
    q.bind(1, m_shiftId);
    if(q.step() != SQLITE_ROW)
    {
        endResetModel();
        return;
    }

    m_shiftName = q.getRows().get<QString>(0);

    q.prepare("SELECT COUNT(1)"
              " FROM jobs"
              " JOIN stops s1 ON s1.id=jobs.firstStop"
              " JOIN stops s2 ON s2.id=jobs.lastStop"
              " WHERE jobs.shiftId=? AND s2.departure>? AND s1.arrival<?");
    q.bind(1, m_shiftId);
    q.bind(2, m_start);
    q.bind(3, m_end);
    q.step();
    int count = q.getRows().get<int>(0) - 1; //Do not count ourself
    m_data.reserve(count);

    q.prepare("SELECT jobs.id,jobs.category,"
              " s1.arrival,"
              " s2.departure"
              " FROM jobs"
              " JOIN stops s1 ON s1.id=jobs.firstStop"
              " JOIN stops s2 ON s2.id=jobs.lastStop"
              " WHERE jobs.shiftId=? AND s2.departure>? AND s1.arrival<?"
              " ORDER BY s1.arrival, jobs.id");
    q.bind(1, m_shiftId);
    q.bind(2, m_start);
    q.bind(3, m_end);

    for(auto j : q)
    {
        JobInfo info;
        info.jobId = j.get<db_id>(0);
        info.jobCat = JobCategory(j.get<int>(1));

        if(info.jobId == m_jobId)
        {
            m_jobCat = info.jobCat;
            continue;
        }

        info.start = j.get<QTime>(3);
        info.end = j.get<QTime>(4);

        m_data.append(info);
    }

    endResetModel();
}

QString ShiftBusyModel::getJobName() const
{
    return JobCategoryName::jobName(m_jobId, m_jobCat);
}
