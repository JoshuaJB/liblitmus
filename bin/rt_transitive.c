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

#define MIN_VERTICES 8
#define MAX_VERTICES 16384
//#define MIN_EDGES 0
//#define MAX_EDGES 268435456
#define MIN_SEED -2147483647
#define MAX_SEED -1
#define NO_PATH 2147483647

#define MIN_EDGES 0
#define MAX_EDGES 255

int loops = 1;
unsigned int *din, *dout;
unsigned int n, n_max;
unsigned int m, m_max;
int seed;

#define MAX_SAMPLES	20000
static cycles_t result[MAX_SAMPLES];
static int result_index = 0;

int init_job(){
  n_max = 35;
  m_max = 200;
  seed = -2;

  assert((n_max >= MIN_VERTICES) && (n_max <= MAX_VERTICES));
  assert((m_max >= MIN_EDGES) && (m_max <= MAX_EDGES));
  assert (m_max <= n_max*n_max);
  assert ((seed >= MIN_SEED) && (seed <= MAX_SEED));

  if ((din = (unsigned int *)malloc(n_max*n_max*sizeof(unsigned int))) == NULL)
    return (-1);
  if ((dout = (unsigned int *)malloc(n_max*n_max*sizeof(unsigned int))) == NULL)
    return (-1);

  srand (time(NULL));
  randInit(seed);

  return 0;
}

int main_job() {
  unsigned int i, j, k;
  
  n = 35;
  m = 200;
 
  for (i=0; i<n_max*n_max; i++){
   *(din + i) = NO_PATH;
   *(dout + i) = NO_PATH;
  }
  
  for (k=0; k<m; k++){
    i = randInt(0, n-1);
    j = randInt(0, n-1);
    *(din + j*n + i) = randInt(MIN_EDGES, MAX_EDGES);
  }
  
  for (k=0; k<n; k++){
  unsigned int old;
  unsigned int new1;
  unsigned int *dtemp;
    
    for (i=0; i<n; i++){
      for (j=0; j<n; j++){
	old = *(din + j*n + i);
	new1 = *(din + j*n + k) + *(din + k*n + i);
	*(dout + j*n + i) = (new1 < old ? new1: old);
	assert (*(dout + j*n + i) <= NO_PATH);
	assert (*(dout + j*n + i) <= *(din + j*n + i));
      }
    }
      dtemp = dout;
      dout = din;
      din = dtemp;
  }

  return 0;
}

int post_job() {
	if (din) {
		free(din);
		din = NULL;
	}
	if (dout) {
		free(dout);
		dout = NULL;
	}

	return(0);
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

#define OPTSTR "p:wk:m:i:b:u:"
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
	
	init_job();
	main_job();
	
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
		printf("%ld\n", result[i]);
	return 0;
}
