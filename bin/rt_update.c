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

#define MAX_SAMPLES	20000

int loops = 1;

struct timeval t1, t2;

unsigned int *field;
unsigned int f_max;
unsigned int idx;
unsigned short int w;
unsigned int maxhops;
int seed;
unsigned int initial;
unsigned int minStop;
unsigned int maxStop;
unsigned int hops;

int init_job(){
  f_max = 204800;
  maxhops = 1024000;
  seed = -2;
  initial = 10;
  minStop = f_max - 1;
  maxStop = minStop;

  assert((f_max >= MIN_FIELD_SIZE) && (f_max <= MAX_FIELD_SIZE));

  
  assert((maxhops >= MIN_HOP_LIMIT) && (maxhops <= MAX_HOP_LIMIT));
  assert((seed >= MIN_SEED) && (seed <= MAX_SEED));
  assert((initial >= 0) && (initial < f_max));
  assert((minStop >= 0) && (minStop < f_max));
  assert((maxStop >= 0) && (maxStop < f_max));

  if ((field = (unsigned int *)malloc(f_max*sizeof(int))) == NULL)
    return (-1);
  srand (time(NULL));
  randInit(seed);

  return 0;
}

int main_job() {
  unsigned int f;
  unsigned int l;
  
	f = 204800; //10240;
	w = 15; 
	maxhops = 40960; //10240;
	minStop = f - 1;
	maxStop = minStop;
	initial = 1;
	
	assert((w >= MIN_WINDOW_SIZE) && (w <= MAX_WINDOW_SIZE));
	assert(w%2 == 1);
	assert(f > w);
	
  for (l=0; l<f; l++){
    field[l] = randInt(0, f-w);
  }
  
  hops = 0;
  idx = initial;

  while ((hops < maxhops) &&
	(!((idx >= minStop) &&
	   (idx < maxStop)))){    
    int sum;

    unsigned int ll, lll;
    unsigned int max, min;
    unsigned int partition;
    unsigned int high;
    max = MAX_FIELD_SIZE;
    min = 0;
    high = 0;
    sum = 0;

    for (ll=0; ll<w; ll++){
      unsigned int balance;
      unsigned int x;
      x = field[idx+ll];
      sum += x;

      if (x > max) high++;
      else if (x >min){ /* start else* */
	partition = x;
	balance = 0;
	for (lll=ll+1; lll<w; lll++){
	  if (field[idx+lll] > partition) balance++;
	} 
	if (balance+high == w/2) break;
	else if (balance+high>w/2){
	  min = partition;
	}/* end if */
	else{
	  max = partition;
	  high++;
	} /* end else */
      }
      if (min == max) break;
    }/* end else* */
    field[idx] = sum % (f-w);
    idx = (partition+hops)%(f-w);
    hops++;
  }/* end for loop */
  return 0;
}

int post_job() {
  free(field);
  return(1);
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
		//register cycles_t t;
		//t = get_cycles();
		while (iter++ < loops) {
			main_job();
		}
		//result[result_index++] = get_cycles() - t;
		sleep_next_period();
		return 1;
	}
}

#define OPTSTR "p:wk:m:i:b:u:l:"
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
		case 'l':
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
	//for (i = 0; i < result_index; i++)
		//printf("%ld\n", result[i]);
	return 0;
}
