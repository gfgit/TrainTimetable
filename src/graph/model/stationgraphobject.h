#ifndef STATIONGRAPHOBJECT_H
#define STATIONGRAPHOBJECT_H

#include <QVector>

#include "utils/types.h"

#include "stations/station_utils.h"
#include <QRgb>

class StationGraphObject
{
public:
    StationGraphObject();

    db_id stationId;
    QString stationName;
    utils::StationType stationType;

    typedef struct
    {
        db_id jobId;
        db_id stopId;
        JobCategory category;
        double arrivalY;
        double departureY;
    } JobGraph;

    typedef struct
    {
        db_id platformId;
        QString platformName;
        QRgb color;
        QFlags<utils::StationTrackType> platformType;
        QVector<JobGraph> jobStops;
    } PlatformGraph;

    QVector<PlatformGraph> platforms;

    double xPos;
};

#endif // STATIONGRAPHOBJECT_H
