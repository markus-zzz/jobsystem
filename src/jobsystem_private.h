#pragma once

#include "jobsystem.h"
#include <pthread.h>

#define JOB_QUEUE_SIZE 4096
#define JOB_POOL_SIZE 4096

struct JobSystem_Job {
	int32_t unfinishedJobs; // atomic
	uint16_t jobFunctionId;
	JobSystem_Job* parent;
	char padding[5];
};

struct WorkStealingQueue {
	struct JobSystem_Job *jobs[JOB_QUEUE_SIZE];
	uint32_t top, bottom;
	pthread_mutex_t mutex;
};

struct JobSystem_Context {
	struct WorkStealingQueue *queues;
	struct JobSystem_WorkerContext *jswc;
	uint32_t n_workers;
};

struct JobSystem_WorkerContext {
	struct JobSystem_Context *jsc;
	struct JobSystem_Job job_pool[JOB_POOL_SIZE];
	uint32_t job_pool_idx;
	uint32_t worker_idx;
};

