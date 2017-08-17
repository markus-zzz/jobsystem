#include "src/jobsystem_private.h"
#include <assert.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>

#define JOBSYSTEM_JOB(x) x,
static const JobSystem_JobFunction JobFunctionId2JobFunction[] = {
#include "jobs.def"
};
#undef JOBSYSTEM_JOB

#define JOBSYSTEM_JOB(x) #x,
static const char *JobFunctionId2Str[] = {
#include "jobs.def"
};
#undef JOBSYSTEM_JOB

JobSystem_Job *JobId2Job(JobSystem_Context *jsc, JobSystem_JobId id)
{
	JobSystem_Job *job = NULL;
	if (id != JOB_ID_NULL) {
		uint32_t widx = id >> 12;
		uint32_t jidx = id & (JOB_POOL_SIZE - 1);
		assert(widx < jsc->n_workers);
		job = &jsc->jswc[widx].job_pool[jidx];
	}
	return job;
}


JobSystem_Job *AllocateJob(JobSystem_WorkerContext *jswc, JobSystem_JobId *jobId)
{
	uint16_t tmp = jswc->job_pool_idx++;
	*jobId = tmp | (jswc->worker_idx << 12);
	return &jswc->job_pool[jswc->worker_idx*JOB_POOL_SIZE + tmp];
}

//
// work queue
//

#define MASK (JOB_QUEUE_SIZE-1)
static void
queue_push(struct WorkStealingQueue *queue, JobSystem_Job *job)
{
	pthread_mutex_lock(&queue->mutex);

	queue->jobs[queue->bottom & MASK] = job;
	++queue->bottom;

	pthread_mutex_unlock(&queue->mutex);
}

static JobSystem_Job *
queue_pop(struct WorkStealingQueue *queue)
{
	JobSystem_Job *job = NULL;
	pthread_mutex_lock(&queue->mutex);
	const int jobCount = queue->bottom - queue->top;
	if (jobCount > 0) {
		--queue->bottom;
		job = queue->jobs[queue->bottom & MASK];
	}
	pthread_mutex_unlock(&queue->mutex);
	return job;
}

static JobSystem_Job *
queue_steal(struct WorkStealingQueue *queue)
{
	JobSystem_Job *job = NULL;
	pthread_mutex_lock(&queue->mutex);
	const int jobCount = queue->bottom - queue->top;
	if (jobCount > 0) {
		job = queue->jobs[queue->top & MASK];
		++queue->top;
	}
	pthread_mutex_unlock(&queue->mutex);
	return job;
}


static JobSystem_Job *
GetJob(JobSystem_WorkerContext *jswc)
{
	struct WorkStealingQueue *ourQueue = &jswc->queue;

	JobSystem_Job *job = queue_pop(ourQueue);
	if (!job)
	{
		/* no job in our queue so resort to stealing */
		unsigned int randomIndex = rand() % jswc->jsc->n_workers;
		struct WorkStealingQueue *stealQueue = &jswc->jsc->jswc[randomIndex].queue;
		if (stealQueue != ourQueue)
		{
			/* only steal from others */
			job = queue_steal(stealQueue);
		}
	}

	return job;
}

static void
Finish(JobSystem_WorkerContext *jswc, JobSystem_Job *job)
{
	int32_t tmp = __atomic_sub_fetch(&job->unfinishedJobs, 1, __ATOMIC_SEQ_CST);
	if (tmp == 0) {
		JobSystem_Job *parent = JobId2Job(jswc->jsc, job->parentJobId);
		if (parent) {
			Finish(jswc, parent);
		}
	}
}

#if JOBSYSTEM_TRACE_ENABLE
static void
Trace(JobSystem_WorkerContext *jswc, JobSystem_Job *job, uint8_t kind)
{
	struct JobSystem_TraceEvent *te = &jswc->jobTrace[jswc->job_trace_idx++ & (JOB_TRACE_SIZE - 1)];
	clock_gettime(CLOCK_MONOTONIC, &te->ts);
	te->kind = kind;
	te->jobFunctionId = job->jobFunctionId;
}
#endif

static void
Execute(JobSystem_WorkerContext *jswc, JobSystem_Job *job)
{
	JobSystem_JobFunction jf = JobFunctionId2JobFunction[job->jobFunctionId];
#if JOBSYSTEM_TRACE_ENABLE
	Trace(jswc, job, 1);
#endif
	jf(jswc, job, job->data);
#if JOBSYSTEM_TRACE_ENABLE
	Trace(jswc, job, 2);
#endif
	Finish(jswc, job);
}

static void *
worker_thread_entry_point(void *arg)
{
	JobSystem_WorkerContext *jswc = arg;
	while (!jswc->shutDownWorker) {
		JobSystem_Job *job = GetJob(jswc);
		if (job) {
			Execute(jswc, job);
		}
		else {
			sched_yield();
		}
	}

	return NULL;
}


JobSystem_WorkerContext *
JobSystem_StartUp(uint16_t n_workers)
{
	assert(sizeof(struct JobSystem_Job) == 64);

	JobSystem_Context *jsc = calloc(sizeof(JobSystem_Context), 1);
	jsc->n_workers = n_workers;
	jsc->jswc = calloc(sizeof(JobSystem_WorkerContext), n_workers);

	for (uint16_t i = 0; i < n_workers; i++) {
		jsc->jswc[i].jsc = jsc;
		jsc->jswc[i].worker_idx = i;
		jsc->jswc[i].job_pool_idx = 0;
	}

	for (uint16_t i = 1; i < n_workers; i++) {
		pthread_create(&jsc->jswc[i].thread, NULL, &worker_thread_entry_point, &jsc->jswc[i]);
	}

	return &jsc->jswc[0];
}

void
JobSystem_ShutDown(JobSystem_WorkerContext *jswc)
{
	assert(jswc->worker_idx == 0);
	JobSystem_Context *jsc = jswc->jsc;
	for (uint16_t i = 1; i < jsc->n_workers; i++) {
		jsc->jswc[i].shutDownWorker = 1;
		pthread_join(jsc->jswc[i].thread, NULL);
	}

	free(jsc->jswc);
	free(jsc);
}

void
JobSystem_Reset(JobSystem_WorkerContext *jswc)
{
	assert(jswc->worker_idx == 0);
	for (uint16_t i = 0; i < jswc->jsc->n_workers; i++) {
		jswc->jsc->jswc[i].job_pool_idx = 0;
	}
}

JobSystem_JobId
JobSystem_CreateJob(JobSystem_WorkerContext *jswc, JobSystem_JobFunctionId jfid)
{
	JobSystem_JobId jobId;
	JobSystem_Job *job = AllocateJob(jswc, &jobId);
	job->jobFunctionId = jfid;
	job->parentJobId = JOB_ID_NULL;
	job->unfinishedJobs = 1;

	return jobId;
}

JobSystem_JobId
JobSystem_CreateChildJob(JobSystem_WorkerContext *jswc, JobSystem_JobId parentJobId, JobSystem_JobFunctionId jfid)
{
	JobSystem_Job *parent = JobId2Job(jswc->jsc, parentJobId);
	__atomic_fetch_add(&parent->unfinishedJobs, 1, __ATOMIC_SEQ_CST);

	JobSystem_JobId jobId;
	JobSystem_Job *job = AllocateJob(jswc, &jobId);
	job->jobFunctionId = jfid;
	job->parentJobId = parentJobId;
	job->unfinishedJobs = 1;

	return jobId;
}

void
JobSystem_SubmitJob(JobSystem_WorkerContext *jswc, JobSystem_JobId jobId, void *data, uint32_t datasize)
{
	JobSystem_Job *job = JobId2Job(jswc->jsc, jobId);
	queue_push(&jswc->queue, job);
}

void
JobSystem_WaitJob(JobSystem_WorkerContext *jswc, JobSystem_JobId jobId)
{
	assert(jswc->worker_idx == 0);

	JobSystem_Job *job = JobId2Job(jswc->jsc, jobId);

	while (__atomic_load_n (&job->unfinishedJobs, __ATOMIC_SEQ_CST) > 0) {
		JobSystem_Job *nextJob = GetJob(jswc);
		if (nextJob) {
			Execute(jswc, nextJob);
		}
	}
}

void
JobSystem_DumpTrace(JobSystem_WorkerContext *jswc, const char *path)
{
#if JOBSYSTEM_TRACE_ENABLE
	JobSystem_Context *jsc = jswc->jsc;
	FILE *fp = fopen(path, "w");

	fprintf(fp, "{\"traceEvents\":[");
	for (uint16_t i = 0; i < jsc->n_workers; i++) {
		for (uint32_t j = 0; j < jsc->jswc[i].job_trace_idx; j++) {
			struct JobSystem_TraceEvent *te = &jsc->jswc[i].jobTrace[j];
			uint64_t ts_in_us = te->ts.tv_sec * 1000000 + te->ts.tv_nsec / 1000;
			if (i != 0 || j != 0) {
				fprintf(fp, ",");
			}
			fprintf(fp, "\n{\"pid\":3890,\"tid\":%d,\"ts\":%lu,\"ph\":\"%c\",\"cat\":\"blink\",\"name\":\"%s\"}", i, ts_in_us, te->kind == 1 ? 'B' : 'E', JobFunctionId2Str[te->jobFunctionId]);
		}
	}
	fprintf(fp, "\n]}\n");

	fclose(fp);
#endif
}
