#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define NUM_PAGES 100

#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <fcntl.h>


#include "litmus.h"
#include "common.h"

static char* progname;
char* buff;
int fd, buffsize;
//struct timespec delta;
static int dma_rate;

#define PAGE_SIZE (4096)
#define CACHELINE_SIZE 32
#define INTS_IN_CACHELINE (CACHELINE_SIZE/sizeof(int))
#define CACHELINES_IN_1KB (1024 / sizeof(cacheline_t))
#define INTS_IN_1KB	(1024 / sizeof(int))
#define INTS_IN_CACHELINE (CACHELINE_SIZE/sizeof(int))

static cacheline_t* arena = NULL;

#define UNCACHE_DEV "/dev/litmus/uncache"
#define FAKE_DEV "/dev/litmus/fakedev0"

static cacheline_t* alloc_arena(size_t size, int use_huge_pages, int use_uncache_pages)
{
	int flags = MAP_PRIVATE | MAP_POPULATE;
	cacheline_t* arena = NULL;
	int fd;

	if(use_huge_pages)
		flags |= MAP_HUGETLB;

	if(use_uncache_pages == 1) {
		fd = open(UNCACHE_DEV, O_RDWR|O_SYNC);
		if (fd == -1)
			bail_out("Failed to open uncache device. Are you running the LITMUS^RT kernel?");
	} else if (use_uncache_pages == 2) {
		fd = open(FAKE_DEV, O_RDWR|O_SYNC);
		if (fd == -1)
			bail_out("Failed to open fake device. Are you running the LITMUS^RT kernel?");
	} else {
		fd = -1;
		flags |= MAP_ANONYMOUS;
	}

	arena = mmap(0, size, PROT_READ | PROT_WRITE, flags, fd, 0);
	
	if(use_uncache_pages)
		close(fd);

	assert(arena);

	return arena;
}

static void dealloc_arena(cacheline_t* arena, size_t size)
{
		int ret = munmap((void*)arena, size);
        if(ret != 0)
                bail_out("munmap() error");
}

static int randrange(int min, int max)
{
        /* generate a random number on the range [min, max) w/o skew */
        int limit = max - min;
        int devisor = RAND_MAX/limit;
        int retval;

        do {
                retval = rand() / devisor;
        } while(retval == limit);
        retval += min;

        return retval;
}

static void init_arena(cacheline_t* arena, size_t size)
{
    int i;
        size_t num_arena_elem = size / sizeof(cacheline_t);

        /* Generate a cycle among the cache lines using Sattolo's algorithm.
           Every int in the cache line points to the same cache line.
           Note: Sequential walk doesn't care about these values. */
        for (i = 0; i < num_arena_elem; i++) {
                int j;
                for(j = 0; j < INTS_IN_CACHELINE; ++j)
                        arena[i].line[j] = i;
        }
        while(1 < i--) {
                int j = randrange(0, i);
                cacheline_t temp = arena[j];
                arena[j] = arena[i];
                arena[i] = temp;
        }
}

/* Random walk around the arena in cacheline-sized chunks.
   Cacheline-sized chucks ensures the same utilization of each
   hit line as sequential read. (Otherwise, our utilization
   would only be 1/INTS_IN_CACHELINE.) */
static int random_walk(cacheline_t *mem, int wss, int write_cycle)
{
	/* a random cycle among the cache lines was set up by init_arena(). */
	int sum, i, next;

	int numlines = wss * CACHELINES_IN_1KB;

	sum = 0;

	/* contents of arena is structured s.t. offsets are all
	   w.r.t. to start of arena, so compute the initial offset */
	next = mem - arena;

	if (write_cycle == 0) {
		for (i = 0; i < numlines; i++) {
			/* every element in the cacheline has the same value */
			next = arena[next].line[0];
			sum += next;
		}
	} else {
		int w, which_line;
		for (i = 0, w = 0; i < numlines; i++) {
			which_line = next;
			next = arena[next].line[0];
			if((w % write_cycle) != (write_cycle - 1)) {
				sum += next;
			}
			else {
				((volatile cacheline_t*)arena)[which_line].line[0] = next;
			}
		}
	}
	return sum;
}

static cacheline_t* random_start(int wss)
{
	return arena + randrange(0, ((wss * 1024)/sizeof(cacheline_t)));
}

static volatile int dont_optimize_me = 0;

int loop_once(int wss) {
	cacheline_t *mem;
	int temp;
	
	mem = random_start(wss);
	temp = random_walk(mem, wss, 1);
	dont_optimize_me = temp;
	
	return dont_optimize_me;
}

int send_dma() {
	int ret = read(fd, buff, buffsize);
	if (ret < 0) {
		perror("read error");
	}
	return ret;
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

/*static int job(double program_end)
{
	if (wctime() > program_end)
		return 0;
	else {
		if (main_job() < 0){
			return 0;
		}
		sleep_next_period();
		return 1;
	}
}
*/

static int job(int wss, double exec_time, double program_end)
{
	double emergency_exit = program_end + 1;
	int iter = 0;
	if (wctime() > program_end)
		return 0;
	else {
		double last_loop = 0, loop_start;
		int tmp = 0;

		double start = cputime();
		double now = cputime();

		while (now + last_loop < start + exec_time) {
			loop_start = now;
			tmp += loop_once(wss);
			//nanosleep(&delta, NULL);
			if (iter % dma_rate == 0)
				tmp += send_dma();
			now = cputime();
			last_loop = now - loop_start;
			if (emergency_exit && wctime() > emergency_exit) {
				/* Oops --- this should only be possible if the execution time tracking
				 * is broken in the LITMUS^RT kernel. */
				fprintf(stderr, "!!! rtspin/%d emergency exit!\n", getpid());
				fprintf(stderr, "Something is seriously wrong! Do not ignore this.\n");
				break;
			}
			iter++;
		}
		
   		sleep_next_period(); 
    	return 1;
	}
}

#define OPTSTR "p:web:d:k:u:"
int main(int argc, char** argv)
{
	int ret;
	lt_t wcet;
	lt_t period;
	lt_t budget;
	double wcet_ms, period_ms, budget_ms;
	unsigned int priority = 0; //guarantee highest priority to this task
	int migrate = 0;
	int cluster = 0;
	int opt;
	int wait = 0;
	int want_enforcement = 0;
	task_class_t class = RT_CLASS_HARD;
	struct rt_task param;
	struct mc2_task mc2_param;
	struct reservation_config config;
	int res_type = PERIODIC_POLLING;
	double duration = 0, start = 0;
	struct control_page* ctl_page = NULL;
	size_t arena_sz;
	int wss, uncacheable;

	progname = argv[0];
	wss = 3072;
	uncacheable = 0;

	/* default for reservation */
	config.id = 0;
	config.priority = LITMUS_NO_PRIORITY;
	config.cpu = -1;
	
	mc2_param.crit = CRIT_LEVEL_B;
	
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
		case 'e':
			want_enforcement = 1;
			break;
		case 'b':
			budget_ms = atof(optarg);
			break;
		case 'd':
			//delta.tv_sec = 0;
			//delta.tv_nsec = atoi(optarg) * 1000;
			dma_rate = atoi(optarg);
			break;
		case 'k':
			wss = atoi(optarg);
			break;
		case 'u':
			uncacheable = atoi(optarg);
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

	if (argc - optind < 2)
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
	
	buffsize = NUM_PAGES*4096;
	buff = (char*) mmap(NULL, buffsize, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buff < 0){
		perror("mmap failed");
		return 0;
	}
	memset(buff, 0, buffsize);
	
	fd = open("/dev/sda", O_RDONLY | O_DIRECT);

	if (migrate) {
		ret = be_migrate_to_domain(cluster);
		if (ret < 0)
			bail_out("could not migrate to target partition or cluster.");
	}
	else{
		bail_out("dio must be migrated.");
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

	/* create reservation */
	ret = reservation_create(res_type, &config);
	if (ret < 0) {
		bail_out("failed to create reservation.");
	}
	
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

	arena_sz = wss*1024;
	arena = alloc_arena(arena_sz*2, 0, uncacheable);
	init_arena(arena, arena_sz);
	loop_once(wss);
	
	init_litmus();
	ctl_page = get_ctrl_page();
	if (!ctl_page)
		bail_out("could not get ctrl_page");
	
	start = wctime();
	ret = task_mode(LITMUS_RT_TASK);
	if (ret != 0)
		bail_out("could not become RT task");

	set_page_color(config.cpu);
	recolor_mem(buff, NUM_PAGES, 3);
	recolor_mem(arena, wss/4, 3);
	
	if (wait) {
		ret = wait_for_ts_release();
		if (ret != 0)
			bail_out("wait_for_ts_release()");
		start = wctime();
	}

	while (job(wss, wcet_ms * 0.001, start + duration)) {};

	ret = task_mode(BACKGROUND_TASK);
	if (ret != 0)
		bail_out("could not become regular task (huh?)");
	
	//test_call(0);
	reservation_destroy(gettid(), config.cpu);
	printf("%s/%d finished.\n",progname, gettid());	
	close(fd);
	munmap(buff, buffsize);
	return 0;
}
