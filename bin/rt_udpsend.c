#include <sys/time.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "UDP_segment.h"
#include "litmus.h"
#include "common.h"

#define MAX_SAMPLES	20000
static cycles_t result[MAX_SAMPLES];
static int result_index = 0;

int sock;
struct sockaddr_in  srv_addr, clt_addr;
int n, addr_len;
char buffer1[MAX_SEGMENT];

int num_trys = 1000;
int trys_left, burst_size;
int segment_size = MAX_SEGMENT;  //default to maximum
char host_name[20] = "127.0.0.1"; // set to use default
short port_number;  /* port */
short my_port = 0;      /* let system assign it */

static inline int loop_once()
{
	int n, trys_left = num_trys ;
	sprintf(buffer1, "Test: sequence=%d ", 1 + num_trys - trys_left);
	n = sendto(sock, buffer1, segment_size, 
			   0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	while ((n < 0) && errno == ENOBUFS) {
		usleep((unsigned int)10);
		n = sendto(sock, buffer1, segment_size, 
						0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	}

	if (n != segment_size) {
		printf("send error, %d\n", n);
		exit(-1);
	}
	//printf("sendto %d completed %d bytes %s\n", 1 + num_trys - trys_left, n, buffer1);
	trys_left -= 1;
	
	return n;
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
		"              [-p PARTITION/CLUSTER [-z CLUSTER SIZE]] [-m CRITICALITY LEVEL]\n"
		"              [-k WSS] [-l LOOPS] [-b BUDGET]\n"
		"\n"
		"WCET and PERIOD are milliseconds, DURATION is seconds.\n");
	exit(EXIT_FAILURE);
}

static int job(double exec_time, double program_end)
{
	if (wctime() > program_end)
		return 0;
	else {
		//double last_loop = 0, loop_start;
		int tmp = 10;
		//double emergency_exit = program_end + 1;

		//double start = cputime();
		//double now = cputime();

		cycles_t t = get_cycles();
		
		/*
		while (now + last_loop < start + exec_time) {
			loop_start = now;
			tmp += loop_once();
			now = cputime();
			last_loop = now - loop_start;
			if (emergency_exit && wctime() > emergency_exit) {
				fprintf(stderr, "!!! rtspin/%d emergency exit!\n", getpid());
				fprintf(stderr, "Something is seriously wrong! Do not ignore this.\n");
				break;
			}
		}*/
		while(tmp-- > 0) {
			loop_once();
			//usleep((unsigned int)1000);
		}
		
		result[result_index++] = get_cycles() - t;
		//printf("job %d\n", result_index);
		sleep_next_period(); 
    	return 1;
	}
}


#define OPTSTR "p:wm:i:b:k:n:u:"
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
	int i;

  	/* default for reservation */
	config.id = 0;
	config.priority = LITMUS_NO_PRIORITY; /* use EDF by default */
	config.cpu = -1;
	
	mc2_param.crit = CRIT_LEVEL_C;
	
	budget_ms = 1000;
	
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
			port_number = atoi(optarg);
			break;
		case 'k':
			segment_size = atoi(optarg);
			break;
		case 'n':
			num_trys = atoi(optarg);
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
	if ((segment_size < 64) || (segment_size > MAX_SEGMENT))
	{
		printf("error - segment size not in range [64,%d]\n", MAX_SEGMENT);
		exit(-1);
	}
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
	
	/* set up address of server to contact */
	bzero((char *) &srv_addr, sizeof(srv_addr)); /* clear it */
	srv_addr.sin_family = AF_INET; /* internet */
	/* set the host address where service runs */
	srv_addr.sin_addr.s_addr = inet_addr(host_name);
	/* set the host port where the service runs */
	srv_addr.sin_port = htons(port_number);

	/* create the socket */
	if ((sock = socket(AF_INET, SOCK_DGRAM,0)) < 0)
		{printf("error - could not create socket\n"); exit(-1);}

	/*  bind a local address  */
	bzero((char *) &clt_addr, sizeof(clt_addr)); /* clear it */
	clt_addr.sin_family = AF_INET;
	clt_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	clt_addr.sin_port = htons(my_port);  

	if (bind(sock, (struct sockaddr *)&clt_addr,
               sizeof(clt_addr)) < 0)
       {printf("error - could not bind\n"); exit(-1);}
	
	loop_once();
	printf("test loop finished\n");
	
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
		
	ret = init_litmus();
	if (ret != 0)
		bail_out("init_litmus() failed\n");

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

	while (job(wcet_ms * 0.001, start + duration)) {};

	ret = task_mode(BACKGROUND_TASK);
	if (ret != 0)
		bail_out("could not become regular task (huh?)");

	reservation_destroy(gettid(), config.cpu);
	//test_call(0);
	printf("Done.\n");

	for ( i = 0; i < result_index; i++)
		printf("%ld\n", result[i]);
	return 0;
}

