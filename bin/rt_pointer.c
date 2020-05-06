#include <sys/time.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <limits.h>


#include "litmus.h"
#include "common.h"
#include "DISstressmarkRNG.h"

#define MIN_FIELD_SIZE 16
#define MAX_FIELD_SIZE 16777216
#define MIN_WINDOW_SIZE 1
#define MAX_WINDOW_SIZE 15
#define MIN_HOP_LIMIT 1

#define MAX_HOP_LIMIT 4294967295U

#define MIN_SEED -2147483647
#define MAX_SEED -1
#define MIN_THREADS 1
#define MAX_THREADS 256

#define MAX_SAMPLES	20000
static cycles_t result[MAX_SAMPLES];
static int result_index = 0;

int loops = 1;

unsigned int *field;
unsigned int f;
unsigned short int w;
unsigned int maxhops;
int seed = -2;
unsigned int n;

clock_t startTime;

struct threadS{
unsigned int initial;
unsigned int minStop;
unsigned int maxStop;
unsigned int hops;
}*thread;

int init_job() {
	int l;
	
	f = 102400;
	w = 9;
	maxhops = 1024;
	n = 3;
	
	assert ((w >= MIN_WINDOW_SIZE) && (w <= MAX_WINDOW_SIZE));
	assert (w % 2 == 1);
	assert (f > w);
 
	assert ((f >= MIN_FIELD_SIZE) && (f <= MAX_FIELD_SIZE));
	assert ((maxhops >= MIN_HOP_LIMIT) && (maxhops <= MAX_HOP_LIMIT));
	assert ((n >= MIN_THREADS) && (n <= MAX_THREADS));
	if ((thread = (struct threadS *)malloc(n*sizeof(struct threadS))) == NULL)
		return (-1);

	if ((field = (unsigned int *)malloc(f*sizeof(int))) == NULL)
		return (-1);

	for (l=0; l<n; l++){
		thread[l].initial = 1;
		thread[l].minStop = f - thread[l].initial;
		thread[l].maxStop = f - thread[l].initial;

		assert ((thread[l].initial >= 0) && (thread[l].initial < f));
		assert ((thread[l].minStop >= 0) && (thread[l].minStop < f));
		assert ((thread[l].maxStop >= 0) && (thread[l].maxStop < f));
	}

	for (l=0; l<f; l++){
		field[l] = randInt(0, f-w);
	}
 
	return 0;
}

int main_job() {
	unsigned int l;

	for (l=0; l<n; l++) {
	  unsigned int index;
	  unsigned int minStop, maxStop;
	  unsigned int hops;
	 
	  hops = 0;
	  minStop = thread[l].minStop;
	  maxStop = thread[l].maxStop;
	  index = thread[l].initial;
	  while ((hops < maxhops) &&
			(!((index >= minStop) &&
			 (index < maxStop)))){

		unsigned int ll, lll;
		unsigned int max, min;
		unsigned int partition;
		unsigned int high;
	 
		partition = field[index];
		max = MAX_FIELD_SIZE;
		min = 0;
		high = 0;
	 
	 for (ll=0; ll<w; ll++){
	   unsigned int balance;
	   unsigned int x;
		x = field[index+ll];

	   if (x > max) high++;
	   else if (x > min){ /* start else* */
		 partition = x;
		 balance = 0;
		 for (lll=ll+1; lll<w; lll++){
		   if (field[index+lll] > partition) balance++;
		 }/* end for loop */

		 if (balance+high == w/2) break;
		 else if (balance+high > w/2){
			  min = partition;
		 }/* end if */
		 else { 
			  max = partition;
			  high++;
		 }/* end else */
	  }
	   if (min == max) break;
	 } /* end else* */
	  index = (partition+hops)%(f-w);
	  hops++;
	 }/* end loop ll */
	  thread[l].hops = hops;
	} /* end while */
 
 return(0);
}

int post_job() {
	if (field) {
		free(field);
		field = NULL;
	}
	if (thread) {
		free(thread);
		thread = NULL;
	}
	return 0;
}

static void usage(char *error) {
	fprintf(stderr, "Error: %s\n", error);
	fprintf(stderr,
		"Usage:\n"
		"	rt_spin [COMMON-OPTS] WCET PERIOD DURATION\n"
		"	rt_spin [COMMON-OPTS] -f FILE [-o COLUMN] WCET PERIOD\n"
		"	rt_spin -l\n"
		"\n"
		"COMMON-OPTS = [-w] [-s SCALE]\n"
		"              [-p PARTITION/CLUSTER [-z CLUSTER SIZE]] [-c CLASS] [-m CRITICALITY LEVEL]\n"
		"\n"
		"WCET and PERIOD are microseconds, DURATION is seconds.\n");
	exit(EXIT_FAILURE);
}

static int job(double exec_time, double program_end)
{
	if (wctime() > program_end)
		return 0;
	else {
		register int iter = 0;
		register cycles_t t;
		t = get_cycles();
		while (iter++ < loops) {
			main_job();
		}
		result[result_index++] = get_cycles() - t;
		sleep_next_period();
		return 1;
	}
}

#define OPTSTR "p:wk:l:m:i:b:u:"
int main(int argc, char** argv)
{
	int ret;
	lt_t wcet;
	lt_t period;
	lt_t budget;
	double wcet_ms, period_ms, budget_ms;
	unsigned int priority = LITMUS_NO_PRIORITY;
	int migrate = 0;
	int cluster = 0;
	int opt;
	int wait = 0;
	int want_enforcement = 0;
	double duration = 0, start = 0;
	double scale = 1.0;
	task_class_t class = RT_CLASS_HARD;
	struct rt_task param;
	struct mc2_task mc2_param;
	struct reservation_config config;
	int res_type = PERIODIC_POLLING;
	int i;

	/* default for reservation */
	config.id = 0;
	config.priority = LITMUS_NO_PRIORITY; /* use EDF by default */
	config.cpu = -1;
	
	mc2_param.crit = CRIT_LEVEL_C;
	
	budget_ms = 10;

	while ((opt = getopt(argc, argv, OPTSTR)) != -1) {
		switch (opt) {
		case 'w':
			wait = 1;
			break;
		case 'p':
			cluster = atoi(optarg);
			migrate = 1;
			config.cpu = cluster;
			break;
		case 'k':
			loops = atoi(optarg);
			break;
		case 'm':
			mc2_param.crit = atoi(optarg);
			if (mc2_param.crit < CRIT_LEVEL_A || mc2_param.crit == NUM_CRIT_LEVELS) {
				usage("Invalid criticality level.");
			}
			res_type = PERIODIC_POLLING;
			break;
		case 'b':
			budget_ms = atof(optarg);
			break;
		case 'u':
			break;
		case 'i':
			config.priority = atoi(optarg);
			break;
		case ':':
			usage("Argument missing.");
			break;
		case '?':
		default:
			usage("Bad argument.");
			break;
		}
	}

	if (mc2_param.crit > CRIT_LEVEL_A && config.priority != LITMUS_NO_PRIORITY)
		usage("Bad criticailty level or priority");

	if (argc - optind < 3)
		usage("Arguments missing.");

	wcet_ms   = atof(argv[optind + 0]);
	period_ms = atof(argv[optind + 1]);

	wcet   = ms2ns(wcet_ms);
	period = ms2ns(period_ms);
	budget = ms2ns(budget_ms);
	
	if (wcet <= 0)
		usage("The worst-case execution time must be a "
				"positive number.");
	if (period <= 0)
		usage("The period must be a positive number.");
	if (wcet > period) {
		usage("The worst-case execution time must not "
				"exceed the period.");
	}

	duration  = atof(argv[optind + 2]);

	if (migrate) {
		ret = be_migrate_to_domain(cluster);
		if (ret < 0)
			bail_out("could not migrate to target partition or cluster.");
	}

	/* reservation config */
	config.id = gettid();
	
	config.polling_params.budget = budget;
	config.polling_params.period = period;
	config.polling_params.offset = 0;
	config.polling_params.relative_deadline = 0;
	if (config.polling_params.budget > config.polling_params.period) {
		usage("The budget must not exceed the period.");
	}
	
	/* create a reservation */
	ret = reservation_create(res_type, &config);
	if (ret < 0) {
		bail_out("failed to create reservation.");
	}
	randInit(-2);
	
	init_job();
	
	init_rt_task_param(&param);
	param.exec_cost = wcet;
	param.period = period;
	param.priority = priority;
	param.cls = class;
	param.release_policy = TASK_PERIODIC;
	param.budget_policy = (want_enforcement) ?
			PRECISE_ENFORCEMENT : NO_ENFORCEMENT;
	if (migrate) {
		param.cpu = gettid();
	}
	ret = set_rt_task_param(gettid(), &param);
	
	if (ret < 0)
		bail_out("could not setup rt task params");
	
	mc2_param.res_id = gettid();
	ret = set_mc2_task_param(gettid(), &mc2_param);

	if (ret < 0)
		bail_out("could not setup mc2 task params");

	init_litmus();
	if (ret != 0)
		bail_out("init_litmus() failed\n");
	
	mlockall(MCL_CURRENT | MCL_FUTURE);
	
	if (mc2_param.crit == CRIT_LEVEL_C)
		set_page_color(-1);
	else
		set_page_color(config.cpu);
	
	start = wctime();
	ret = task_mode(LITMUS_RT_TASK);

	if (ret != 0)
		bail_out("could not become RT task");


	if (wait) {
		ret = wait_for_ts_release();
		if (ret != 0)
			bail_out("wait_for_ts_release()");
		start = wctime();
	}

	while (job(wcet_ms * 0.001 * scale, start + duration)) {};

	ret = task_mode(BACKGROUND_TASK);
	if (ret != 0)
		bail_out("could not become regular task (huh?)");

	reservation_destroy(gettid(), config.cpu);
	post_job();
	//printf("%s/%d finished.\n",progname, gettid());	
	for (i = 0; i < result_index; i++)
		printf("%lld\n", result[i]);
	return 0;
}
