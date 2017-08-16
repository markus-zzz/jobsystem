#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "jobsystem.h"

void foobar(JobSystem_WorkerContext *jswc, JobSystem_Job *job, const void *arg)
{
	for (volatile int i = 0; i < 1024; i++);
}

int main(int argc, char **argv)
{
	JobSystem_WorkerContext *jswc = JobSystem_Create(4);

	JobSystem_JobId root = JobSystem_CreateJob(jswc, JOBSYSTEM_JOBID_foobar);

	for (unsigned i = 0; i < 128; i++) {
		JobSystem_JobId job0 = JobSystem_CreateChildJob(jswc, root, JOBSYSTEM_JOBID_foobar);
		JobSystem_SubmitJob(jswc, job0, NULL, 0);
	}

	JobSystem_SubmitJob(jswc, root, NULL, 0);
	JobSystem_WaitJob(jswc, root);

	JobSystem_DumpTrace(jswc, "trace.json");

	return 0;
}
