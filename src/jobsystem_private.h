#pragma once

#include "jobsystem.h"
#include <pthread.h>
#include <time.h>

#define JOB_QUEUE_SIZE 4096
#define JOB_POOL_SIZE 4096
#define JOB_CONT_SIZE 16
#define JOB_DATA_SIZE 20

#define JOB_ID_NULL 0xffffu

#if JOBSYSTEM_TRACE_ENABLE
#define JOB_TRACE_SIZE 1024
struct JobSystem_TraceEvent {
	struct timespec ts;
	uint16_t jobFunctionId;
	uint8_t kind;
};
#endif

struct JobSystem_Job {
	int32_t unfinishedJobs; /* atomic */
	int32_t continuationCount; /* atomic */
	uint16_t jobFunctionId;
	uint16_t parentJobId;
	uint16_t continuations[JOB_CONT_SIZE];
	uint8_t data[JOB_DATA_SIZE];
};

struct WorkStealingQueue {
	struct JobSystem_Job *jobs[JOB_QUEUE_SIZE];
	uint32_t top, bottom;
	pthread_mutex_t mutex;
};

struct JobSystem_Context {
	struct JobSystem_WorkerContext *jswc;
	uint16_t n_workers;
};

struct JobSystem_WorkerContext {
	struct JobSystem_Context *jsc;
	struct WorkStealingQueue queue;
	struct JobSystem_Job job_pool[JOB_POOL_SIZE];
	uint32_t job_pool_idx;
	uint32_t worker_idx;
	pthread_t thread;
	uint32_t shutDownWorker;
#if JOBSYSTEM_TRACE_ENABLE
	struct JobSystem_TraceEvent jobTrace[JOB_TRACE_SIZE];
	uint32_t job_trace_idx;
#endif
};


