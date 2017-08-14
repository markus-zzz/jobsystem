#pragma once

#include <stdint.h>

struct JobSystem_Context;
struct JobSystem_WorkerContext;
struct JobSystem_Job;

typedef struct JobSystem_Context JobSystem_Context;
typedef struct JobSystem_WorkerContext JobSystem_WorkerContext;
typedef struct JobSystem_Job JobSystem_Job;

typedef void (*JobSystem_JobFunction)(JobSystem_WorkerContext *jswc, JobSystem_Job*, const void*);


JobSystem_WorkerContext *
JobSystem_Create(uint32_t n_workers);

void
JobSystem_Destroy(JobSystem_Context *jsc);

JobSystem_Job *
JobSystem_CreateJob(JobSystem_WorkerContext *jswc, JobSystem_JobFunction func);

JobSystem_Job *
JobSystem_CreateChildJob(JobSystem_WorkerContext *jswc, JobSystem_Job *parent, JobSystem_JobFunction func);

void
JobSystem_SubmitJob(JobSystem_WorkerContext *jswc, JobSystem_Job *job, void *data, uint32_t datasize);

void
JobSystem_WaitJob(JobSystem_WorkerContext *jswc, JobSystem_Job *job);
