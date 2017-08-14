#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "jobsystem.h"

static void jobf0(JobSystem_WorkerContext *jswc, JobSystem_Job *job, const void *arg)
{
	printf("This is jobf0 (%lu)\n", pthread_self());
}

int main(int argc, char **argv)
{
	JobSystem_WorkerContext *jswc = JobSystem_Create(4);

	JobSystem_Job *root = JobSystem_CreateJob(jswc, jobf0);

	for (unsigned i = 0; i < 128; i++) {
		JobSystem_Job *job0 = JobSystem_CreateChildJob(jswc, root, jobf0);
		JobSystem_SubmitJob(jswc, job0, NULL, 0);
	}

	JobSystem_SubmitJob(jswc, root, NULL, 0);
	JobSystem_WaitJob(jswc, root);

	return 0;
}
