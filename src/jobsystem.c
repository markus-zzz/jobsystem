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
		job = &jsc->job_pools[widx*JOB_POOL_SIZE + jidx];
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
	struct WorkStealingQueue *ourQueue = jswc->queue;

	JobSystem_Job *job = queue_pop(ourQueue);
	if (!job)
	{
		/* no job in our queue so resort to stealing */
		unsigned int randomIndex = rand() % jswc->jsc->n_workers;
		struct WorkStealingQueue *stealQueue = &jswc->jsc->queues[randomIndex];
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

static void
Execute(JobSystem_WorkerContext *jswc, JobSystem_Job *job)
{
	JobSystem_JobFunction jf = JobFunctionId2JobFunction[job->jobFunctionId];
	printf("Worker #%d begin '%s'\n", jswc->worker_idx, JobFunctionId2Str[job->jobFunctionId]);
	jf(jswc, job, job->data);
	printf("Worker #%d end '%s'\n", jswc->worker_idx, JobFunctionId2Str[job->jobFunctionId]);
	Finish(jswc, job);
}

static void *
worker_thread_entry_point(void *arg)
{
	JobSystem_WorkerContext *jswc = arg;
	while (1) {
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
JobSystem_Create(uint16_t n_workers)
{
	assert(sizeof(struct JobSystem_Job) == 64);

	JobSystem_Context *jsc = calloc(sizeof(JobSystem_Context), 1);
	jsc->queues = calloc(sizeof(struct WorkStealingQueue), n_workers);
	jsc->job_pools = calloc(sizeof(struct JobSystem_Job), n_workers*JOB_POOL_SIZE);
	jsc->n_workers = n_workers;

	jsc->jswc = calloc(sizeof(JobSystem_WorkerContext), n_workers);
	for (uint16_t i = 0; i < n_workers; i++) {
		jsc->jswc[i].jsc = jsc;
		jsc->jswc[i].worker_idx = i;
		jsc->jswc[i].job_pool_idx = 0;
		jsc->jswc[i].queue = &jsc->queues[i];
		jsc->jswc[i].job_pool = &jsc->job_pools[i * JOB_POOL_SIZE];
	}

	for (uint16_t i = 1; i < n_workers; i++) {
		pthread_t thread_id;
		pthread_create(&thread_id, NULL, &worker_thread_entry_point, &jsc->jswc[i]);
	}

	return &jsc->jswc[0];
}

void
JobSystem_Destroy(JobSystem_Context *jsc)
{
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
	queue_push(jswc->queue, job);
}

void
JobSystem_WaitJob(JobSystem_WorkerContext *jswc, JobSystem_JobId jobId)
{
	JobSystem_Job *job = JobId2Job(jswc->jsc, jobId);

	while (__atomic_load_n (&job->unfinishedJobs, __ATOMIC_SEQ_CST) > 0) {
		JobSystem_Job *nextJob = GetJob(jswc);
		if (nextJob) {
			Execute(jswc, nextJob);
		}
	}
}
