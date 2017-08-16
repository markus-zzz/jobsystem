#pragma once

#include <stdint.h>

#define JOBSYSTEM_JOB(x) JOBSYSTEM_JOBID_##x,
typedef enum JobSystem_JobFunctionId {
#include "jobs.def"
} JobSystem_JobFunctionId;
#undef JOBSYSTEM_JOB

struct JobSystem_Context;
struct JobSystem_WorkerContext;
struct JobSystem_Job;

typedef struct JobSystem_Context JobSystem_Context;
typedef struct JobSystem_WorkerContext JobSystem_WorkerContext;
typedef struct JobSystem_Job JobSystem_Job;
typedef uint16_t JobSystem_JobId;

typedef void (*JobSystem_JobFunction)(JobSystem_WorkerContext *jswc, JobSystem_Job*, const void*);


JobSystem_WorkerContext *
JobSystem_Create(uint16_t n_workers);

void
JobSystem_Destroy(JobSystem_Context *jsc);

JobSystem_JobId
JobSystem_CreateJob(JobSystem_WorkerContext *jswc, JobSystem_JobFunctionId jfid);

JobSystem_JobId
JobSystem_CreateChildJob(JobSystem_WorkerContext *jswc, JobSystem_JobId parentJobId, JobSystem_JobFunctionId jfid);

void
JobSystem_SubmitJob(JobSystem_WorkerContext *jswc, JobSystem_JobId jobId, void *data, uint32_t datasize);

void
JobSystem_WaitJob(JobSystem_WorkerContext *jswc, JobSystem_JobId jobId);

void
JobSystem_DumpTrace(JobSystem_WorkerContext *jswc, const char *path);

#define JOBSYSTEM_JOB(x) void x(JobSystem_WorkerContext *jswc, JobSystem_Job*, const void*);
#include "jobs.def"
#undef JOBSYSTEM_JOB
