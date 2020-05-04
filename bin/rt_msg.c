#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/mman.h>

#include "litmus.h"
#include "common.h"

#define MAX_SAMPLES	50000
#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

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

static volatile int dont_optimize_me = 0;

cycles_t s_result[MAX_SAMPLES], r_result[MAX_SAMPLES];
float s_data[MAX_SAMPLES], r_data[MAX_SAMPLES];
int s_index = 0, r_index = 0;
int s_outlier = 0, r_outlier = 0;
float s_max = 0.0, r_max = 0.0, s_avg = 0.0, r_avg = 0.0;
int NUM_SAMPLES = 1000;
int temp[MAX_SAMPLES];
static int loops = 100;
long mtype;
int msg_size;
int queue_id;
struct msgbuf *send_buf, *recv_buf;
float z = 3.89; // for confidence interval 99 = 2.58 99.99 = 3.89
	
static int loop_once()
{
	int i, rc;
	cycles_t t;
	
	send_buf->mtype = mtype;
	for (i=0;i<msg_size;i++) {
		send_buf->mtext[i] = 'a';
	}
	
	//test_call(2);
	t = get_cycles();
	rc = msgsnd(queue_id, send_buf, msg_size, 0);
	s_result[s_index++] = get_cycles() - t;

	if (rc == -1) {
		perror("main: msgsnd");
		exit(1);
	}
	
	/* do some thing here */
/*	for (i=0;i<MAX_SAMPLES;i++) {
		temp[i] = rand()%MAX_SAMPLES;
		dont_optimize_me += temp[i];
	}
*/
	
	//test_call(2);
	t = get_cycles();
	rc = msgrcv(queue_id, recv_buf, msg_size, 0, 0);
	r_result[r_index++] = get_cycles() - t;

	if (rc == -1) {
		perror("main: msgrcv");
	exit(1);
	}

	if ((r_result[r_index-1] > 10000) || (s_result[s_index-1] > 10000)) {
		r_index--;
		s_index--;
	}
		
	return dont_optimize_me;
}

float calculate_stdev(float data[], int n_data, float *mean)
{
    float sum = 0.0, me, sdev = 0.0;

    int i;

    for(i = 0; i < n_data; ++i)
    {
        sum += data[i];
    }

    me = sum/n_data;

    for(i = 0; i < n_data; ++i)
        sdev += pow(data[i] - me, 2);

	*mean = me;
    return sqrt(sdev / n_data);
}

static int job(double exec_time, double program_end)
{
	if ((wctime() > program_end)) // || (result_index > MAX_SAMPLES))
		return 0;
	else {
		register unsigned int iter = 0;
		while(iter++ < loops) {
			loop_once();
		}
		sleep_next_period(); 
    	return 1;
	}
}

#define OPTSTR "p:wl:m:i:b:k:u:a:k:"
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
	//FILE *fp;
	
	queue_id = msgget(IPC_PRIVATE, 0600);
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
		case 'l':
			loops = atoi(optarg);
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
			mtype = atol(optarg);
			break;
		case 'k':
			msg_size = atoi(optarg);
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


	send_buf = (struct msgbuf*)malloc(sizeof(struct msgbuf)+msg_size);
	recv_buf = (struct msgbuf*)malloc(sizeof(struct msgbuf)+msg_size);
	
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
	
	for (i = 0 ; i<NUM_SAMPLES; i++) {
        s_result[i] = 0;
        r_result[i] = 0;
    }
	
	loop_once();
	s_index = 0;
	r_index = 0;
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
	free(send_buf);
	free(recv_buf);

	printf("#%s finished.\n", argv[0]);
    
    for (i = 0 ; i<s_index; i++) {
		//float st_dev, mean;
		s_data[i] = (float)s_result[i]/3.0;
		/*
		st_dev = calculate_stdev(s_data, s_index, &mean);
		if ((s_data[i] < mean - (z*(st_dev/sqrt(s_index)))) || (s_data[i] > mean + (z*(st_dev/sqrt(s_index))))) {
			s_outlier++;
			continue;
		}
		*/
		s_avg += s_data[i];
		if (s_data[i]>s_max)
			s_max = s_data[i];
	}
	s_avg = s_avg / s_index;
    for (i = 0 ; i<r_index; i++) {
		//float st_dev, mean;
		r_data[i] = (float)r_result[i]/3.0;
		/*
		st_dev = calculate_stdev(r_data, r_index, &mean);
		if ((r_data[i] < mean - (z*(st_dev/sqrt(r_index)))) || (r_data[i] > mean + (z*(st_dev/sqrt(r_index))))) {
			r_outlier++;
			continue;
		}
		*/
		r_avg += r_data[i];
		if (r_data[i]>r_max)
			r_max = r_data[i];
	}
	r_avg = r_avg / r_index;
	
	/*
	fp = fopen("data.log", "w");
	
	for (i=0;i<min(s_index,r_index);i++)
		fprintf(fp, "%f,%f\n", s_data[i], r_data[i]);
	fclose(fp);
	*/
	printf("mtype = %ld msg_size = %d\n", mtype, msg_size);
	printf("Send(%d/%d) max: %f, avg %f\n", s_index-s_outlier, s_index, s_max, s_avg);
	printf("Recv(%d/%d) max: %f, avg %f\n", r_index-r_outlier, r_index, r_max, r_avg);
    return 0;
}
