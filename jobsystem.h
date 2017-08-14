#pragma once

struct JobSystem_Context;

typedef struct JobSystem_Context JobSystem_Context;

JobSystem_Context *
JobSystem_Create(uint32_t n_workers);

void
JobSystem_Destroy(JobSystem_Context *jsc);

JobSystem_Job *
JobSystem_CreateJob(JobSystem_Context *jsc, JobSystem_Function *func);

JobSystem_Job *
JobSystem_CreateSubJob(JobSystem_Context *jsc, JobSystem_Job *job, JobSystem_Function *func);

void
JobSystem_SubmitJob(JobSystem_Context *jsc, JobSystem_Job *job);

void
JobSystem_WaitJob(JobSystem_Context *jsc, JobSystem_Job *job);
