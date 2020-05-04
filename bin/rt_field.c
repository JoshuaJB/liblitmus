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
#define MIN_SEED -2147483647
#define MAX_SEED -1
#define MIN_MOD_OFFSET 0
#define MAX_MOD_OFFSET 65535
#define MIN_TOKENS 1
#define MAX_TOKENS 256
#define MIN_TOKEN_LENGTH 1
#define MAX_TOKEN_LENGTH 8
#define MIN_TOKEN_VALUE 0
#define MAX_TOKEN_VALUE 255
#define MAX_SUBFIELDS 256

#define MAX_SAMPLES	20000

int loops = 1;

struct timeval t1, t2;

struct tokenS{
	unsigned char delimiter[MAX_TOKEN_LENGTH];
	unsigned char length; 
	struct statisticS{
		unsigned int count;
		unsigned char min;
		unsigned char sum;
	} stat[MAX_SUBFIELDS];
	unsigned char subfields;
} token[MAX_TOKENS];

unsigned char *field;
unsigned int f_max;
int seed;
int mod_offset;
unsigned int n_max;

unsigned char input_token[8] = {0x1, 0x1, 0x22, 0x1, 0xc2, 0x1, 0x2d, 0x0};

int init_job(){
  f_max = 262144;
  seed = -2;
  n_max = 128;
    
  assert((seed >= MIN_SEED) && (seed <= MAX_SEED));
  
  if ((field = (unsigned char*)malloc(f_max*sizeof(unsigned char))) == NULL)
    return (-1);

  randInit(seed);

  return 0;
}

int main_job() {
  unsigned int l, f, n;

  f = 102400;
  mod_offset = 256;
  n = 1;

  assert((f >= MIN_FIELD_SIZE) && (f <= MAX_FIELD_SIZE));
  assert((mod_offset >= MIN_MOD_OFFSET) && (mod_offset <= MAX_MOD_OFFSET));
  assert((n >= MIN_TOKENS) && (n <= MAX_TOKENS));
  
  	for (l=0; l<n; l++){
		int index;
		for (index = 0; index<MAX_TOKEN_LENGTH; index++) {
			unsigned char x = input_token[index];
			assert((x >= MIN_TOKEN_VALUE) && (x <= MAX_TOKEN_VALUE));
			token[l].delimiter[index] = (unsigned char )x;
		}
    token[l].length = index;
    }
  
  for (l =0; l<f; l++){
    field[l] = randInt(MIN_TOKEN_VALUE, MAX_TOKEN_VALUE);
  }
  
  for (l =0; l<n; l++){
    unsigned int index;
    
    token[l].subfields = 0;
    token[l].stat[0].count = 0;
    token[l].stat[0].sum = 0;
    token[l].stat[0].min = MAX_TOKEN_VALUE;

    index = 0;
    while ((index < f) && (token[l].subfields < MAX_SUBFIELDS)){
      unsigned char offset;
      offset = 0;
      while ((field[index+offset] == token[l].delimiter[offset]) &&
	     (offset < token[l].length)){
		offset++;
      }

      if (offset == token[l].length){
	    for (offset=0; offset<token[l].length; offset++){
			field[index+offset] = (field[index+offset] + 
				 field[(index+offset+mod_offset) % f])
	                         %(MAX_TOKEN_VALUE+1);
		}
	    index += token[l].length-1;
	    token[l].subfields++;
	    token[l].stat[token[l].subfields].count = 0;
	    token[l].stat[token[l].subfields].sum = 0;
	    token[l].stat[token[l].subfields].min = MAX_TOKEN_VALUE;
      }	     	     	       
	  else {
       	token[l].stat[token[l].subfields].count++;
		token[l].stat[token[l].subfields].sum += field[index];
		if (token[l].stat[token[l].subfields].min > field[index])
			token[l].stat[token[l].subfields].min = field[index];
	  }	     
	index++;
    }
  }

  return 0;
}

int post_job() {
  if (field) {
	free(field);
	field = NULL;
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
		case 'i':
			config.priority = atoi(optarg);
			break;
		case 'u':
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
	//printf("%s/%d finished.\n",argv[0], gettid());	
	//for (i = 0; i < result_index; i++)
	//	printf("%ld\n", result[i]);
	return 0;
}
