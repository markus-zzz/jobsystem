typedef void (*JobFunction)(Job*, const void*);

struct Job
{
	JobFunction function;
	Job* parent;
	int32_t unfinishedJobs; // atomic
	char padding[];
};

static Job g_jobAllocator[MAX_JOB_COUNT];
static uint32_t g_allocatedJobs = 0u;

Job* AllocateJob(void)
{
	const uint32_t index = atomic::Increment(&g_allocatedJobs);
	return &g_jobAllocator[(index-1) % MAX_JOB_COUNT];
}

static const unsigned int NUMBER_OF_JOBS = 4096u;
static const unsigned int MASK = NUMBER_OF_JOBS - 1u;

void Push(Job* job)
{
	ScopedLock lock(criticalSection);

	m_jobs[m_bottom & MASK] = job;
	++m_bottom;
}

Job* Pop(void)
{
	ScopedLock lock(criticalSection);

	const int jobCount = m_bottom - m_top;
	if (jobCount <= 0)
	{
		// no job left in the queue
		return nullptr;
	}

	--m_bottom;
	return m_jobs[m_bottom & MASK];
}

Job* Steal(void)
{
	ScopedLock lock(criticalSection);

	const int jobCount = m_bottom - m_top;
	if (jobCount <= 0)
	{
		// no job there to steal
		return nullptr;
	}

	Job* job = m_jobs[m_top & MASK];
	++m_top;
	return job;
}

Job* CreateJob(JobFunction function)
{
	Job* job = AllocateJob();
	job->function = function;
	job->parent = nullptr;
	job->unfinishedJobs = 1;

	return job;
}

Job* CreateJobAsChild(Job* parent, JobFunction function)
{
	atomic::Increment(&parent->unfinishedJobs);

	Job* job = AllocateJob();
	job->function = function;
	job->parent = parent;
	job->unfinishedJobs = 1;

	return job;
}

void Run(Job* job)
{
	WorkStealingQueue* queue = GetWorkerThreadQueue();
	queue->Push(job);
}

void Wait(const Job* job)
{
	// wait until the job has completed. in the meantime, work on any other job.
	while (!HasJobCompleted(job))
	{
		Job* nextJob = GetJob();
		if (nextJob)
		{
			Execute(nextJob);
		}
	}
}

// main function of each worker thread
while (workerThreadActive)
{
	Job* job = GetJob();
	if (job)
	{
		Execute(job);
	}
}

Job* GetJob(void)
{
	WorkStealingQueue* queue = GetWorkerThreadQueue();

	Job* job = queue->Pop();
	if (IsEmptyJob(job))
	{
		// this is not a valid job because our own queue is empty, so try stealing from some other queue
		unsigned int randomIndex = GenerateRandomNumber(0, g_workerThreadCount+1);
		WorkStealingQueue* stealQueue = g_jobQueues[randomIndex];
		if (stealQueue == queue)
		{
			// don't try to steal from ourselves
			Yield();
			return nullptr;
		}

		Job* stolenJob = stealQueue->Steal();
		if (IsEmptyJob(stolenJob))
		{
			// we couldn't steal a job from the other queue either, so we just yield our time slice for now
			Yield();
			return nullptr;
		}

		return stolenJob;
	}

	return job;
}

void Execute(Job* job)
{
	(job->function)(job, job->data);
	Finish(job);
}
