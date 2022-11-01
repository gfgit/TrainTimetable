#include "job_crossing_data.h"

JobCrossingErrorMap::JobCrossingErrorMap()
{

}

void JobCrossingErrorMap::removeJob(db_id jobId)
{
    auto job = map.find(jobId);
    if(job == map.end())
        return; //Not contained in map

    //Remove all errors referencing to us
    for(const JobCrossingErrorData& err : qAsConst(job->errors))
    {
        auto otherJob = map.find(err.otherJob.jobId);
        if(otherJob == map.end())
            continue; //Maybe already remove, skip

        //Remove all errors regarding job in otherJob
        std::remove_if(otherJob->errors.begin(),
                       otherJob->errors.end(),
                       [jobId](const JobCrossingErrorData& otherErr) -> bool
                       {
                           return otherErr.otherJob.jobId == jobId;
                       });

        if(otherJob->errors.isEmpty())
        {
            //otherJob has no errors, remove it
            map.erase(otherJob);
        }
    }

    //Remove job
    map.erase(job);
}

void JobCrossingErrorMap::merge(const ErrorMap &results)
{
    for(const JobCrossingErrorList& list : results)
    {
        //First clear Job
        removeJob(list.job.jobId);

        //Then add new errors if not empty (already duplicated)
        if(!list.errors.isEmpty())
            map.insert(list.job.jobId, list);
    }
}
