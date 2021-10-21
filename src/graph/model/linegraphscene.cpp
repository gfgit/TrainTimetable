#include "linegraphscene.h"

#include "app/session.h"

#include <sqlite3pp/sqlite3pp.h>

#include <QDebug>

//TODO: maybe move to utils?
constexpr qreal MSEC_PER_HOUR = 1000 * 60 * 60;

static inline qreal timeToHourFraction(const QTime &t)
{
    qreal ret = t.msecsSinceStartOfDay() / MSEC_PER_HOUR;
    return ret;
}

static inline double stationPlatformPosition(const StationGraphObject& st, const db_id platfId, const double platfOffset)
{
    double x = st.xPos;
    for(const StationGraphObject::PlatformGraph& platf : st.platforms)
    {
        if(platf.platformId == platfId)
            return x;
        x += platfOffset;
    }
    return -1;
}

LineGraphScene::LineGraphScene(sqlite3pp::database &db, QObject *parent) :
    QObject(parent),
    mDb(db),
    graphObjectId(0),
    graphType(LineGraphType::NoGraph)
{

}

void LineGraphScene::recalcContentSize()
{
    contentSize = QSize();

    if(graphType == LineGraphType::NoGraph)
        return; //Nothing to draw

    if(stationPositions.isEmpty())
        return;

    const auto entry = stationPositions.last();
    const int platfCount = stations.value(entry.stationId).platforms.count();

    //Add an additional half station offset after last station
    //This gives extra space to center station label
    const int maxWidth = entry.xPos + platfCount * Session->platformOffset + Session->stationOffset / 2;
    const int lastY = Session->vertOffset + Session->hourOffset * 24 + 10;

    contentSize = QSize(maxWidth, lastY);
}

void LineGraphScene::reload()
{
    loadGraph(graphObjectId, graphType, true);
}

bool LineGraphScene::loadGraph(db_id objectId, LineGraphType type, bool force)
{
    if(!force && objectId == graphObjectId && type == graphType)
        return true; //Already loaded

    //Initial state is invalid
    graphType = LineGraphType::NoGraph;
    graphObjectId = 0;
    graphObjectName.clear();
    stations.clear();
    stationPositions.clear();
    contentSize = QSize();

    if(type == LineGraphType::NoGraph)
    {
        //Nothing to load
        emit graphChanged(int(graphType), graphObjectId);
        emit redrawGraph();
        return true;
    }

    if(!mDb.db())
    {
        qWarning() << "Database not open on graph loading!";
        return false;
    }

    if(objectId <= 0)
    {
        qWarning() << "Invalid object ID on graph loading!";
        return false;
    }

    //Leave on left horizOffset plus half station offset to separate first station from HourPanel
    //and to give more space to station label.
    const double curPos = Session->horizOffset + Session->stationOffset / 2;

    if(type == LineGraphType::SingleStation)
    {
        StationGraphObject st;
        st.stationId = objectId;
        if(!loadStation(st))
            return false;

        //Register a single station at start position
        st.xPos = curPos;
        stations.insert(st.stationId, st);
        stationPositions = {{st.stationId, 0, st.xPos, {}}};
        graphObjectName = st.stationName;
    }
    else if(type == LineGraphType::RailwaySegment)
    {
        //TODO: maybe show also station gates
        StationGraphObject stA, stB;

        sqlite3pp::query q(mDb,
                           "SELECT s.in_gate_id,s.out_gate_id,s.name,s.max_speed_kmh,"
                           "s.type,s.distance_meters,"
                           "g1.station_id,g2.station_id"
                           " FROM railway_segments s"
                           " JOIN station_gates g1 ON g1.id=s.in_gate_id"
                           " JOIN station_gates g2 ON g2.id=s.out_gate_id"
                           " WHERE s.id=?");
        q.bind(1, objectId);
        if(q.step() != SQLITE_ROW)
        {
            qWarning() << "Graph: invalid segment ID" << objectId;
            return false;
        }

        auto r = q.getRows();
        //        TODO useful?
        //        outFromGateId = r.get<db_id>(0);
        //        outToGateId = r.get<db_id>(1);
        graphObjectName = r.get<QString>(2);
        //        outSpeed = r.get<int>(3);
        //        outType = utils::RailwaySegmentType(r.get<db_id>(4));
        //        outDistance = r.get<int>(5);

        stA.stationId = r.get<db_id>(6);
        stB.stationId = r.get<db_id>(7);

        if(!loadStation(stA) || !loadStation(stB))
            return false;

        stA.xPos = curPos;
        stB.xPos = stA.xPos + stA.platforms.count() * Session->platformOffset + Session->stationOffset;

        stations.insert(stA.stationId, stA);
        stations.insert(stB.stationId, stB);

        stationPositions = {{stA.stationId, objectId, stA.xPos, {}},
                            {stB.stationId, 0, stB.xPos, {}}};
    }
    else if(type == LineGraphType::RailwayLine)
    {
        if(!loadFullLine(objectId))
        {
            stations.clear();
            stationPositions.clear();
            return false;
        }
    }

    graphObjectId = objectId;
    graphType = type;

    recalcContentSize();

    reloadJobs();

    emit graphChanged(int(graphType), graphObjectId);
    emit redrawGraph();

    return true;
}

bool LineGraphScene::reloadJobs()
{
    if(graphType == LineGraphType::NoGraph)
        return false;

    //TODO: maybe only load visible

    for(StationGraphObject& st : stations)
    {
        if(!loadStationJobStops(st))
            return false;
    }

    //Save last station from previous iteration
    auto lastSt = stations.constEnd();

    for(int i = 0; i < stationPositions.size(); i++)
    {
        StationPosEntry& stPos = stationPositions[i];
        if(!stPos.segmentId)
            continue; //No segment, skip

        db_id fromStId = stPos.stationId;
        db_id toStId = 0;
        if(i <= stationPositions.size() - 1)
            toStId = stationPositions.at(i + 1).stationId;

        if(!toStId)
            break; //No next station

        auto fromSt = lastSt;
        if(fromSt == stations.constEnd() || fromSt->stationId != fromStId)
        {
            fromSt = stations.constFind(fromStId);
            if(fromSt == stations.constEnd())
            {
                continue;
            }
        }

        auto toSt = stations.constFind(toStId);
        if(toSt == stations.constEnd())
            continue;

        if(!loadSegmentJobs(stPos, fromSt.value(), toSt.value()))
            return false;

        //Store last station
        lastSt = toSt;
    }

    return true;
}

JobEntry LineGraphScene::getJobAt(const QPointF &pos, const double tolerance)
{
    const double platformOffset = Session->platformOffset;

    JobEntry job;
    job.jobId = 0;
    job.category = JobCategory::FREIGHT;

    if(stationPositions.isEmpty())
        return job;

    db_id prevStId = 0;
    db_id nextStId = 0;

    for(const StationPosEntry& stPos : qAsConst(stationPositions))
    {
        if(stPos.xPos <= pos.x())
            prevStId = stPos.stationId;

        if(stPos.xPos >= pos.x())
        {
            //We went past the requested position
            nextStId = stPos.stationId;
            break;
        }
    }

    auto prevSt = stations.constFind(prevStId);
    auto nextSt = stations.constFind(nextStId);

    if(prevSt == stations.constEnd() && nextSt == stations.constEnd())
        return job; //Error

    const StationGraphObject::PlatformGraph *prevPlatf = nullptr;
    const StationGraphObject::PlatformGraph *nextPlatf = nullptr;
    double prevPos = 0;
    double nextPos = 0;

    double xPos = 0;
    if(prevSt != stations.constEnd())
    {
        xPos = prevSt->xPos;
        for(const StationGraphObject::PlatformGraph& platf : prevSt->platforms)
        {
            if(xPos <= pos.x())
            {
                prevPlatf = &platf;
                prevPos = xPos;
            }
            if(xPos >= pos.x())
            {
                //We went past the requested position
                nextPlatf = &platf;
                nextPos = xPos;
                break;
            }

            xPos += platformOffset;
        }
    }

    const double prevDistance = qAbs(prevPos - pos.x());
    if(prevPlatf && prevDistance > tolerance)
    {
        //Discard because too distant
        prevPlatf = nullptr;
    }

    if(!nextPlatf && nextSt != stations.constEnd())
    {
        //Use second station
        xPos = nextSt->xPos;
        for(const StationGraphObject::PlatformGraph& platf : nextSt->platforms)
        {
            if(xPos >= pos.x())
            {
                nextPlatf = &platf;
                nextPos = xPos;
                break;
            }

            xPos += platformOffset;
        }
    }

    const double nextDistance = qAbs(nextPos - pos.x());
    if(nextPlatf && nextDistance > tolerance)
    {
        //Discard because too distant
        nextPlatf = nullptr;
    }

    const StationGraphObject::PlatformGraph *resultPlatf = nullptr;
    if(!prevPlatf)
        resultPlatf = nextPlatf;
    else if(!nextPlatf)
        resultPlatf = prevPlatf;
    else
    {
        if(prevDistance < nextDistance)
            resultPlatf = prevPlatf;
        else
            resultPlatf = nextPlatf;
    }

    if(!resultPlatf)
        return job; //No match

    for(const StationGraphObject::JobStopGraph& jobStop : resultPlatf->jobStops)
    {
        if(jobStop.arrivalY <= pos.y() && jobStop.departureY >= pos.y())
        {
            //Found match
            job.jobId = jobStop.jobId;
            job.category = jobStop.category;
            break;
        }
    }

    return job;
}

bool LineGraphScene::loadStation(StationGraphObject& st)
{
    sqlite3pp::query q(mDb);

    q.prepare("SELECT name,short_name,type FROM stations WHERE id=?");
    q.bind(1, st.stationId);
    if(q.step() != SQLITE_ROW)
    {
        qWarning() << "Graph: invalid station ID" << st.stationId;
        return false;
    }

    //Load station
    auto row = q.getRows();

    st.stationName = row.get<QString>(1);
    if(st.stationName.isEmpty())
    {
        //Empty short name, fallback to full name
        st.stationName = row.get<QString>(0);
    }
    st.stationType = utils::StationType(row.get<int>(2));

    //Load platforms
    const QRgb white = qRgb(255, 255, 255);
    q.prepare("SELECT id, type, color_rgb, name FROM station_tracks WHERE station_id=? ORDER BY pos");
    q.bind(1, st.stationId);
    for(auto r : q)
    {
        StationGraphObject::PlatformGraph platf;
        platf.platformId = r.get<db_id>(0);
        platf.platformType = utils::StationTrackType(r.get<int>(1));
        if(r.column_type(2) == SQLITE_NULL) //NULL is white (#FFFFFF) -> default value
            platf.color = white;
        else
            platf.color = QRgb(r.get<int>(2));
        platf.platformName = r.get<QString>(3);
        st.platforms.append(platf);
    }

    return true;
}

bool LineGraphScene::loadFullLine(db_id lineId)
{
    //TODO: maybe show also station gates
    //TODO: load only visible stations, other will be loaded when scrolling graph
    sqlite3pp::query q(mDb, "SELECT name FROM lines WHERE id=?");
    q.bind(1, lineId);
    if(q.step() != SQLITE_ROW)
    {
        qWarning() << "Graph: invalid line ID" << lineId;
        return false;
    }

    //Store line name
    graphObjectName = q.getRows().get<QString>(0);

    //Get segments
    q.prepare("SELECT ls.id, ls.seg_id, ls.direction,"
              "seg.name, seg.max_speed_kmh, seg.type, seg.distance_meters,"
              "g1.station_id, g2.station_id"
              " FROM line_segments ls"
              " JOIN railway_segments seg ON seg.id=ls.seg_id"
              " JOIN station_gates g1 ON g1.id=seg.in_gate_id"
              " JOIN station_gates g2 ON g2.id=seg.out_gate_id"
              " WHERE ls.line_id=?"
              " ORDER BY ls.pos");
    q.bind(1, lineId);

    db_id lastStationId = 0;
    double curPos = Session->horizOffset + Session->stationOffset / 2;

    for(auto seg : q)
    {
        db_id lineSegmentId = seg.get<db_id>(0);
        db_id railwaySegmentId = seg.get<db_id>(1);
        bool reversed = seg.get<int>(2) != 0;

        //item.segmentName = seg.get<QString>(3);
        //item.maxSpeedKmH = seg.get<int>(4);
        //item.segmentType = utils::RailwaySegmentType(seg.get<int>(5));
        //item.distanceMeters = seg.get<int>(6);

        //Store first segment end
        db_id fromStationId = seg.get<db_id>(7);

        //Store also the other end of segment for last item
        db_id otherStationId = seg.get<db_id>(8);

        if(reversed)
        {
            //Swap segments ends
            qSwap(fromStationId, otherStationId);
        }

        if(!lastStationId)
        {
            //First line station
            StationGraphObject st;
            st.stationId = fromStationId;
            if(!loadStation(st))
                return false;

            st.xPos = curPos;
            stations.insert(st.stationId, st);
            stationPositions.append({st.stationId, railwaySegmentId, st.xPos, {}});
            curPos += st.platforms.count() * Session->platformOffset + Session->stationOffset;
        }
        else if(fromStationId != lastStationId)
        {
            qWarning() << "Line segments are not adjacent, ID:" << lineSegmentId
                       << "LINE:" << lineId;
            return false;
        }

        StationGraphObject stB;
        stB.stationId = otherStationId;
        if(!loadStation(stB))
            return false;

        stB.xPos = curPos;
        stations.insert(stB.stationId, stB);

        stationPositions.last().segmentId = railwaySegmentId;
        stationPositions.append({stB.stationId, 0, stB.xPos, {}});

        curPos += stB.platforms.count() * Session->platformOffset + Session->stationOffset;
        lastStationId = stB.stationId;
    }

    return true;
}

bool LineGraphScene::loadStationJobStops(StationGraphObject &st)
{
    //Reset previous job graphs
    for(StationGraphObject::PlatformGraph& platf : st.platforms)
    {
        platf.jobStops.clear();
    }

    sqlite3pp::query q(mDb, "SELECT stops.id, stops.job_id, jobs.category,"
                            "stops.arrival, stops.departure,"
                            "g_in.track_id, g_out.track_id"
                            " FROM stops"
                            " JOIN jobs ON stops.job_id=jobs.id"
                            " LEFT JOIN station_gate_connections g_in ON g_in.id=stops.in_gate_conn"
                            " LEFT JOIN station_gate_connections g_out ON g_out.id=stops.out_gate_conn"
                            " WHERE stops.station_id=?"
                            " ORDER BY stops.arrival");
    q.bind(1, st.stationId);

    const double vertOffset = Session->vertOffset;
    const double hourOffset = Session->hourOffset;

    for(auto stop : q)
    {
        StationGraphObject::JobStopGraph jobStop;
        jobStop.stopId = stop.get<db_id>(0);
        jobStop.jobId = stop.get<db_id>(1);
        jobStop.category = JobCategory(stop.get<int>(2));
        QTime arrival = stop.get<QTime>(3);
        QTime departure = stop.get<QTime>(4);
        db_id trackId = stop.get<db_id>(5);
        db_id outTrackId = stop.get<db_id>(6);

        if(trackId && outTrackId && trackId != outTrackId)
        {
            //Not last stop, neither first stop. Tracks must correspond
            qWarning() << "Stop:" << jobStop.stopId << "Track not corresponding, using in";
        }
        else if(!trackId)
        {
            if(outTrackId)
                trackId = outTrackId; //First stop, use out gate connection
            else
            {
                qWarning() << "Stop:" << jobStop.stopId << "Both in/out track NULL, skipping";
                continue; //Skip this stop
            }
        }

        StationGraphObject::PlatformGraph *platf = nullptr;

        //Find platform
        for(StationGraphObject::PlatformGraph& p : st.platforms)
        {
            if(p.platformId == trackId)
            {
                platf = &p;
                break;
            }
        }

        if(!platf)
        {
            //Requested platform is not in this station
            qWarning() << "Stop:" << jobStop.stopId << "Track is not in this station";
            continue; //Skip this stop
        }

        //Calculate coordinates
        jobStop.arrivalY = vertOffset + timeToHourFraction(arrival) * hourOffset;
        jobStop.departureY = vertOffset + timeToHourFraction(departure) * hourOffset;

        platf->jobStops.append(jobStop);
    }

    return true;
}

bool LineGraphScene::loadSegmentJobs(LineGraphScene::StationPosEntry& stPos, const StationGraphObject& fromSt, const StationGraphObject& toSt)
{
    //Reset previous job segment graph
    stPos.nextSegmentJobGraphs.clear();

    const double vertOffset = Session->vertOffset;
    const double hourOffset = Session->hourOffset;
    const double platfOffset = Session->platformOffset;

    sqlite3pp::query q(mDb, "SELECT sub.*, jobs.category, g_out.track_id, g_in.track_id FROM ("
                            " SELECT stops.id AS cur_stop_id, lead(stops.id, 1) OVER win AS next_stop_id,"
                            " stops.station_id,"
                            " stops.job_id,"
                            " stops.departure, lead(stops.arrival, 1) OVER win AS next_stop_arrival,"
                            " stops.out_gate_conn,"
                            " lead(stops.in_gate_conn, 1) OVER win AS next_stop_g_in,"
                            " seg_conn.seg_id"
                            " FROM stops"
                            " LEFT JOIN railway_connections seg_conn ON seg_conn.id=stops.next_segment_conn_id"
                            " WINDOW win AS (PARTITION BY stops.job_id ORDER BY stops.arrival)"
                            ") AS sub"
                            " JOIN station_gate_connections g_out ON g_out.id=sub.out_gate_conn"
                            " JOIN station_gate_connections g_in ON g_in.id=sub.next_stop_g_in"
                            " JOIN jobs ON jobs.id=sub.job_id"
                            " WHERE sub.seg_id=?");

    q.bind(1, stPos.segmentId);
    for(auto stop : q)
    {
        JobSegmentGraph job;
        job.fromStopId = stop.get<db_id>(0);
        job.toStopId = stop.get<db_id>(1);
        db_id stId = stop.get<db_id>(2);
        job.jobId = stop.get<db_id>(3);
        QTime departure = stop.get<QTime>(4);
        QTime arrival = stop.get<QTime>(5);
        //6 - out gate connection
        //7 - in gate connection
        //8 - segment_id
        job.category = JobCategory(stop.get<int>(9));
        job.fromPlatfId = stop.get<db_id>(10);
        job.toPlatfId = stop.get<db_id>(11);

        if(toSt.stationId == stId)
        {
            //Job goes in opposite direction, reverse stops
            qSwap(job.fromStopId, job.toStopId);
            qSwap(job.fromPlatfId, job.toPlatfId);
            qSwap(departure, arrival);
        }

        //Calculate coordinates
        job.fromDeparture.rx() = stationPlatformPosition(fromSt, job.fromPlatfId, platfOffset);
        job.fromDeparture.ry() = vertOffset + timeToHourFraction(departure) * hourOffset;

        job.toArrival.rx() = stationPlatformPosition(toSt, job.toPlatfId, platfOffset);
        job.toArrival.ry() = vertOffset + timeToHourFraction(arrival) * hourOffset;

        if(job.fromDeparture.x() < 0 || job.toArrival.x() < 0)
            continue; //Skip, couldn't find platform

        stPos.nextSegmentJobGraphs.append(job);
    }

    return true;
}