#include <sys/time.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <fcntl.h>

#include "litmus.h"
#include "common.h"

#define PAGE_SIZE (4096)
#define CACHELINE_SIZE 32
#define INTS_IN_CACHELINE (CACHELINE_SIZE/sizeof(int))
#define CACHELINES_IN_1KB (1024 / sizeof(cacheline_t))
#define INTS_IN_1KB	(1024 / sizeof(int))
#define INTS_IN_CACHELINE (CACHELINE_SIZE/sizeof(int))

static int loops = 100;
static cacheline_t* arena = NULL;

#define MAX_SAMPLES	20000
static cycles_t result[MAX_SAMPLES];
static int result_index = 0;

#define UNCACHE_DEV "/dev/litmus/uncache"
#define FAKE_DEV "/dev/litmus/fakedev0"

static cacheline_t* alloc_arena(size_t size, int use_huge_pages, int use_uncache_pages)
{
	int flags = MAP_PRIVATE | MAP_POPULATE;
	cacheline_t* arena = NULL;
	int fd;

	if(use_huge_pages)
		flags |= MAP_HUGETLB;

	if (use_uncache_pages == 1) {
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
	}
	
	else {
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

static int sequential_walk(cacheline_t *_mem, int wss, int write_cycle)
{
	int sum = 0, i;
	int* mem = (int*)_mem; /* treat as raw buffer of ints */
	int num_ints = wss * INTS_IN_1KB;

	if (write_cycle > 0) {
		for (i = 0; i < num_ints; i++) {
			if (i % write_cycle == (write_cycle - 1))
				mem[i]++;
			else
				sum += mem[i];
		}
	} else {
		/* sequential access, pure read */
		for (i = 0; i < num_ints; i++)
			sum += mem[i];
	}
	return sum;
}

static cacheline_t* sequential_start(int wss)
{
	static int pos = 0;

	int num_cachelines = wss * CACHELINES_IN_1KB;

	cacheline_t *mem;

	/* Don't allow re-use between allocations.
	 * At most half of the arena may be used
	 * at any one time.
	 */
	if (num_cachelines * 2 > ((wss * 1024)/sizeof(cacheline_t)))
		;//bail_out("static memory arena too small");

	if (pos + num_cachelines > ((wss * 1024)/sizeof(cacheline_t))) {
		/* wrap to beginning */
		mem = arena;
		pos = num_cachelines;
	} else {
		mem = arena + pos;
		pos += num_cachelines;
	}

	return mem;
}

static volatile int dont_optimize_me = 0;

static void usage(char *error) {
	fprintf(stderr, "Error: %s\n", error);
	fprintf(stderr,
		"Usage:\n"
		"	rt_spin [COMMON-OPTS] WCET PERIOD DURATION\n"
		"	rt_spin [COMMON-OPTS] -f FILE [-o COLUMN] WCET PERIOD\n"
		"	rt_spin -l\n"
		"\n"
		"COMMON-OPTS = [-w] [-s SCALE]\n"
		"              [-p PARTITION/CLUSTER [-z CLUSTER SIZE]] [-m CRITICALITY LEVEL]\n"
		"              [-k WSS] [-l LOOPS] [-b BUDGET]\n"
		"\n"
		"WCET and PERIOD are milliseconds, DURATION is seconds.\n");
	exit(EXIT_FAILURE);
}

static int loop_once(int wss, int actype)
{
	cacheline_t *mem;
	int temp;
	
	if (actype == 0) {
		mem = random_start(wss);
		temp = random_walk(mem, wss, 1);
	} else {
		mem = sequential_start(wss);
		temp = sequential_walk(mem, wss, 1);
	}
	dont_optimize_me = temp;

	return dont_optimize_me;
}

static int job(int wss, int actype, double program_end)
{
	if ((wctime() > program_end)) // || (result_index > MAX_SAMPLES))
		return 0;
	else {
		register cycles_t t;
		register unsigned int iter = 0;
		t = get_cycles();
		while(iter++ < loops) {
			loop_once(wss, actype);
		}
		t = get_cycles() - t;
		result[result_index++] = t;
   		sleep_next_period(); 
    	return 1;
	}
}

#define OPTSTR "p:wl:m:i:b:k:u:a:"
int main(int argc, char** argv)
{
	int ret;
	lt_t wcet, period, budget;
	double wcet_ms, period_ms, budget_ms;
	unsigned int priority = LITMUS_NO_PRIORITY;
	int migrate = 0;
	int cluster = 0;
	int opt;
	int wait = 0;
	double duration = 0, start = 0;
	struct rt_task param;
	struct mc2_task mc2_param;
	struct reservation_config config;
	int res_type = PERIODIC_POLLING;
	size_t arena_sz;
	int wss, actype = 0; // default random walk
	int uncacheable = 0;

	/* default for reservation */
	config.id = 0;
	config.priority = LITMUS_NO_PRIORITY; /* use EDF by default */
	config.cpu = -1;
	
	mc2_param.crit = CRIT_LEVEL_C;
	
	budget_ms = 1000;
	wss = 32;
	
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
		case 'k':
			wss = atoi(optarg);
			break;
		case 'm':
			mc2_param.crit = atoi(optarg);
			if ((mc2_param.crit >= CRIT_LEVEL_A) && (mc2_param.crit <= CRIT_LEVEL_C)) {
				res_type = PERIODIC_POLLING;
			}
			else
				usage("Invalid criticality level.");
			break;
		case 'b':
			budget_ms = atof(optarg);
			break;
		case 'i':
			config.priority = atoi(optarg);
			break;
		case 'u':
			uncacheable = atoi(optarg);
			break;
		case 'a':
			actype = atoi(optarg);
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
	srand(getpid());

	/*
	 * We need three parameters
	 */
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
	
	init_rt_task_param(&param);
	param.exec_cost = wcet;
	param.period = period;
	param.priority = priority;
	param.cls = RT_CLASS_HARD;
	param.release_policy = TASK_PERIODIC;
	param.budget_policy = NO_ENFORCEMENT;
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
	
	ret = init_litmus();
	if (ret != 0)
		bail_out("init_litmus() failed\n");
	
	loop_once(wss, actype);
	
	if (mc2_param.crit == CRIT_LEVEL_C)
		set_page_color(-1);
	else
		set_page_color(config.cpu);

	mlockall(MCL_CURRENT | MCL_FUTURE);
	
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

	while (job(wss, actype, start + duration)) {};

	ret = task_mode(BACKGROUND_TASK);
	if (ret != 0)
		bail_out("could not become regular task (huh?)");

	reservation_destroy(gettid(), config.cpu);
	//test_call(0);
	dealloc_arena(arena, arena_sz*2);
	
	printf("#%s finished.\n", argv[0]);
	//for (i = 0; i < result_index; i++)
		//printf("%.2f\n", ((float)result[i])/3.0);

	return 0;
}
