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

#ifndef JOBCROSSINGMODEL_H
#define JOBCROSSINGMODEL_H

#ifdef ENABLE_BACKGROUND_MANAGER

#    include "utils/singledepthtreemodelhelper.h"

#    include "job_crossing_data.h"

class JobCrossingModel;
typedef SingleDepthTreeModelHelper<JobCrossingModel, JobCrossingErrorMap, JobCrossingErrorData>
  JobCrossingModelBase;

// TODO: make on-demand
class JobCrossingModel : public JobCrossingModelBase
{
    Q_OBJECT

public:
    enum Columns
    {
        JobName = 0,
        StationName,
        Arrival,
        Departure,
        Description,
        NCols
    };

    JobCrossingModel(QObject *parent = nullptr);

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;

    void setErrors(const QMap<db_id, JobCrossingErrorList> &data);

    void mergeErrors(const JobCrossingErrorMap::ErrorMap &errMap);

    void clear();

    void removeJob(db_id jobId);

    void renameJob(db_id newJobId, db_id oldJobId);

    void renameStation(db_id stationId, const QString &name);
};

#endif // ENABLE_BACKGROUND_MANAGER

#endif // JOBCROSSINGMODEL_H
