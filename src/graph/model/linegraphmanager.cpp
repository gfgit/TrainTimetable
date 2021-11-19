#include "linegraphmanager.h"

#include "linegraphscene.h"

#include "app/session.h"
#include "viewmanager/viewmanager.h"

LineGraphManager::LineGraphManager(QObject *parent) :
    QObject(parent),
    activeScene(nullptr)
{
    auto session = Session;
    //Stations
    connect(session, &MeetingSession::stationNameChanged, this, &LineGraphManager::onStationNameChanged);
    connect(session, &MeetingSession::stationPlanChanged, this, &LineGraphManager::onStationPlanChanged);
    connect(session, &MeetingSession::stationRemoved, this, &LineGraphManager::onStationRemoved);

    //Segments
    connect(session, &MeetingSession::segmentNameChanged, this, &LineGraphManager::onSegmentNameChanged);
    connect(session, &MeetingSession::segmentStationsChanged, this, &LineGraphManager::onSegmentStationsChanged);
    connect(session, &MeetingSession::segmentRemoved, this, &LineGraphManager::onSegmentRemoved);

    //Lines
    connect(session, &MeetingSession::lineNameChanged, this, &LineGraphManager::onLineNameChanged);
    connect(session, &MeetingSession::lineSegmentsChanged, this, &LineGraphManager::onLineSegmentsChanged);
    connect(session, &MeetingSession::lineRemoved, this, &LineGraphManager::onLineRemoved);

    //Settings
    connect(&AppSettings, &MRTPSettings::jobGraphOptionsChanged, this, &LineGraphManager::updateGraphOptions);
}

void LineGraphManager::registerScene(LineGraphScene *scene)
{
    Q_ASSERT(!scenes.contains(scene));

    scenes.append(scene);

    connect(scene, &LineGraphScene::destroyed, this, &LineGraphManager::onSceneDestroyed);
    connect(scene, &LineGraphScene::sceneActivated, this, &LineGraphManager::setActiveScene);
    connect(scene, &LineGraphScene::jobSelected, this, &LineGraphManager::onJobSelected);
}

void LineGraphManager::unregisterScene(LineGraphScene *scene)
{
    Q_ASSERT(scenes.contains(scene));

    scenes.removeOne(scene);

    disconnect(scene, &LineGraphScene::destroyed, this, &LineGraphManager::onSceneDestroyed);
    disconnect(scene, &LineGraphScene::sceneActivated, this, &LineGraphManager::setActiveScene);
    disconnect(scene, &LineGraphScene::jobSelected, this, &LineGraphManager::onJobSelected);

    //Reset active scene if it is unregistered
    if(activeScene == scene)
        setActiveScene(nullptr);
}

void LineGraphManager::clearAllGraphs()
{
    for(LineGraphScene *scene : qAsConst(scenes))
    {
        scene->loadGraph(0, LineGraphType::NoGraph, true);
    }
}

void LineGraphManager::clearGraphsOfObject(db_id objectId, LineGraphType type)
{
    for(LineGraphScene *scene : qAsConst(scenes))
    {
        if(scene->getGraphObjectId() == objectId && scene->getGraphType() == type)
            scene->loadGraph(0, LineGraphType::NoGraph);
    }
}

void LineGraphManager::setActiveScene(LineGraphScene *scene)
{
    if(activeScene == scene)
        return;

    activeScene = scene;
    emit activeSceneChanged(activeScene);
}

void LineGraphManager::onSceneDestroyed(QObject *obj)
{
    LineGraphScene *scene = static_cast<LineGraphScene *>(obj);
    unregisterScene(scene);
}

void LineGraphManager::onStationNameChanged(db_id stationId)
{
    onStationPlanChanged(stationId); //FIXME: update only labels
}

void LineGraphManager::onStationPlanChanged(db_id stationId)
{
    //FIXME: speed up with threads???
    for(LineGraphScene *scene : qAsConst(scenes))
    {
        if(scene->stations.contains(stationId))
            scene->reload();
    }
}

void LineGraphManager::onStationRemoved(db_id stationId)
{
    clearGraphsOfObject(stationId, LineGraphType::SingleStation);
}

void LineGraphManager::onSegmentNameChanged(db_id segmentId)
{
    onSegmentStationsChanged(segmentId); //FIXME: update only labels
}

void LineGraphManager::onSegmentStationsChanged(db_id segmentId)
{
    //FIXME: speed up with threads???
    for(LineGraphScene *scene : qAsConst(scenes))
    {
        if(scene->graphType == LineGraphType::RailwaySegment && scene->graphObjectId == segmentId)
            scene->reload();
        else if(scene->graphType == LineGraphType::RailwayLine)
            scene->reload(); //FIXME: only if inside this line
        //Single stations are not affected
    }
}

void LineGraphManager::onSegmentRemoved(db_id segmentId)
{
    clearGraphsOfObject(segmentId, LineGraphType::RailwaySegment);
}

void LineGraphManager::onLineNameChanged(db_id lineId)
{
    onLineSegmentsChanged(lineId); //FIXME: update only labels
}

void LineGraphManager::onLineSegmentsChanged(db_id lineId)
{
    //FIXME: speed up with threads???
    for(LineGraphScene *scene : qAsConst(scenes))
    {
        if(scene->graphType == LineGraphType::RailwayLine && scene->graphObjectId == lineId)
            scene->reload();
    }
}

void LineGraphManager::onLineRemoved(db_id lineId)
{
    clearGraphsOfObject(lineId, LineGraphType::RailwayLine);
}

void LineGraphManager::onJobSelected(db_id jobId)
{
    //TODO: maybe allow multiple job editing, one per open LineGraphScene
    if(jobId)
        Session->getViewManager()->requestJobEditor(jobId);
    else
        Session->getViewManager()->requestClearJob();
}

void LineGraphManager::updateGraphOptions()
{
    //TODO: maybe get rid of theese variables in MeetingSession and always use AppSettings?
    int hourOffset = AppSettings.getHourOffset();
    Session->hourOffset = hourOffset;

    int horizOffset = AppSettings.getHorizontalOffset();
    Session->horizOffset = horizOffset;

    int vertOffset = AppSettings.getVerticalOffset();
    Session->vertOffset = vertOffset;

    Session->stationOffset = AppSettings.getStationOffset();
    Session->platformOffset = AppSettings.getPlatformOffset();

    Session->jobLineWidth = AppSettings.getJobLineWidth();

    //Reload all graphs
    for(LineGraphScene *scene : qAsConst(scenes))
    {
        scene->reload();
    }
}
