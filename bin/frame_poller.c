// This program will take a single snapshot from the webcam and store it as a
// PPM file named "snapshot.ppm".
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

#include "litmus.h"
#include "common.h"
#include "webcam_lib.h"

// This is the maximum number of supported resolutions to check for when
// searching for the highest supported resolution of the device. This shouldn't
// need to be adjusted.
#define MAX_RESOLUTIONS_TO_CHECK (64)

#define MAX_SAMPLES	20000
struct times {
	cycles_t t1;
	cycles_t t2;
};

static struct times result[MAX_SAMPLES];
static int result_index = 0;

static volatile int dont_optimize_me = 0;
static int num_dropped_frames = 0;
static int loop_mode = 0;

static uint32_t w;
static uint32_t h;

// Sets the resolution to the given width and height. Prints a list of
// available resolutions and returns an error if the selected resolution isn't
// supported.
static int SelectResolution(WebcamInfo *webcam, int res_index) {
  int i, resolution_ok;
  WebcamResolution resolutions[MAX_RESOLUTIONS_TO_CHECK];
  memset(resolutions, 0, sizeof(resolutions));
  if (!GetSupportedResolutions(webcam, resolutions,
    MAX_RESOLUTIONS_TO_CHECK)) {
    printf("Failed getting the webcam's supported resolutions.\n");
    return 0;
  }
  
  w = resolutions[res_index].width;
  h = resolutions[res_index].height;
  
  // Make sure the chosen resolution is in the list of resolutions.
  resolution_ok = 0;
  for (i = 0; i < MAX_RESOLUTIONS_TO_CHECK; i++) {
    if ((resolutions[i].width == 0) && (resolutions[i].height == 0)) break;
    if (resolutions[i].width != w) continue;
    if (resolutions[i].height != h) continue;
    resolution_ok = 1;
    break;
  }
  // The resolution wasn't found: print out a list of available choices and
  // return failure.
  if (!resolution_ok) {
    printf("Invalid resolution selected: %d x %d.\n", w, h);
    printf("Available resolutions:\n");
    for (i = 0; i < MAX_RESOLUTIONS_TO_CHECK; i++) {
      if ((resolutions[i].width == 0) && (resolutions[i].height == 0)) break;
      printf("  %d x %d\n", resolutions[i].width, resolutions[i].height);
    }
    return 0;
  }
  printf("#%d x %d Resolution selected.\n", w, h);
  if (!SetResolution(webcam, w, h)) {
    printf("Failed setting the webcam resolution.\n");
    return 0;
  }
  return 1;
}


// Takes a pointer to a Webcam info struct, the number of times to poll for a
// frame, and the number of seconds to wait between each frame. Returns 0 if
// any error occurs and nonzero otherwise. Each frame will be converted to the
// RGBA color profile and placed in the rgba_buffer buffer. This memory must
// be pre-allocated and have sufficient space to hold 4 bytes per pixel. Also
// provides a count of dropped frames.
static int loop_once(WebcamInfo *webcam, uint8_t *rgba_buffer) {
	
	int i;
	uint8_t *frame_buffer = NULL;
	int retry = 10000;
	size_t frame_size;
	cycles_t t1, t2, t3;
	volatile int tmp = 0;
	FrameBufferState frame_state = DEVICE_ERROR;

	t1 = get_cycles();
	
	if (!BeginLoadingNextFrame(webcam)) {
		printf("Failed to request a new frame.\n");
		return 0;
	}

/*
	t2 = get_cycles();
	while (get_cycles()-t2 < 10000) {
		tmp++;
	}
*/	
	for (i = 0 ; i < retry; i++) {
		frame_state = GetFrameBuffer(webcam, (void**)&frame_buffer, &frame_size);
	
		if (frame_state == FRAME_READY) {
			if (rgba_buffer == NULL)
				return 1; // debug loop
			t2 = get_cycles();
			
			memcpy(rgba_buffer, frame_buffer, frame_size);
			
			// to grayscale
/*
			for (y = 0; y < h; y++) {
				for (x = 0; x < w; x += 2) {
				  rgba_buffer[0] = frame_buffer[0];
				  rgba_buffer[1] = frame_buffer[2];
				  frame_buffer += 4;
				  rgba_buffer += 2;
				}
			}
*/
			t3 = get_cycles();
			break;
		} else if (frame_state == FRAME_NOT_READY) {
			// If the frame wasn't ready after sleeping for the requested delay, just
			// drop it and wait for another delay.
			//dropped_frames++;
			;
		} else if (frame_state != FRAME_NOT_READY) {
			// Return an error if a frame was unavailable for any reason other than
			// not being ready yet.
			printf("Error loading a frame.\n");
			return 0;
		}
		t2 = get_cycles();
		while (get_cycles()-t2 < 1000) {
			tmp++;
		}
	}
	dont_optimize_me = tmp;
	result[result_index].t1 = t2-t1;
	result[result_index].t2 = t3-t2;
	result_index++;
	
	if (i == retry && frame_state != FRAME_READY) {
		num_dropped_frames++;
	}
	//printf("retry %d\n", i);
	return dont_optimize_me;
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


static int job(WebcamInfo *webcam, uint8_t *rgba_buffer, double exec_time, double program_end)
{
	if (wctime() > program_end)
		return 0;
	else {
		//register cycles_t t;
		//t = get_cycles();
		loop_once(webcam, rgba_buffer);
		//t = get_cycles() - t;
		//printf("%ld cycles\n", t);
		//result[result_index++] = get_cycles() - t;
		//test_call(2);
   		sleep_next_period(); 
    	return 1;
	}
}

static int job_loop(WebcamInfo *webcam, uint8_t *rgba_buffer, double exec_time, double program_end)
{
	if (wctime() > program_end)
		return 0;
	else {
		double last_loop = 0, loop_start;
		int tmp = 0;
		double emergency_exit = program_end + 1;

		double start = cputime();
		double now = cputime();

		while (now + last_loop < start + exec_time) {
			loop_start = now;
			tmp += loop_once(webcam, rgba_buffer);
			now = cputime();
			last_loop = now - loop_start;
			if (emergency_exit && wctime() > emergency_exit) {
				/* Oops --- this should only be possible if the execution time tracking
				 * is broken in the LITMUS^RT kernel. */
				fprintf(stderr, "!!! rtspin/%d emergency exit!\n", getpid());
				fprintf(stderr, "Something is seriously wrong! Do not ignore this.\n");
				break;
			}
		}
		
		//test_call(2);
		sleep_next_period(); 
    	return 1;
	}
}

static void PrintError(void) {
  printf("Last known error: %s (%d)\n", strerror(errno), errno);
}

#define OPTSTR "p:wm:i:b:k:u:d:l:"
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
	int res_index; 
	char dev_name[255];
	
	WebcamInfo webcam;
	uint8_t *rgba_buffer = NULL;
	
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
		case 'k':
			res_index = atoi(optarg);
			break;
		case 'u':
			sprintf(dev_name, "%s", optarg);
			break;
		case 'l':
			loop_mode = atoi(optarg);
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
	
	if (!OpenWebcam(dev_name, &webcam)) {
		printf("Failed opening the webcam.\n");
		PrintError();
		return 1;
	}
	printf("#Webcam opened.\n");
	if (!SelectResolution(&webcam, res_index)) {
		PrintError();
		return 1;
	}
	rgba_buffer = (uint8_t *) malloc(w * h * 4);
	if (!rgba_buffer) {
		printf("Failed allocating memory for converted frame.\n");
		CloseWebcam(&webcam);
		return 1;
	}
	GetResolution(&webcam, &w, &h);
	loop_once(&webcam, NULL);
	
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

	if (loop_mode == 0) {
		while (job(&webcam, rgba_buffer, wcet_ms * 0.001, start + duration)) {};
	} else {
		while (job_loop(&webcam, rgba_buffer, wcet_ms * 0.001, start + duration)) {};
	}

	ret = task_mode(BACKGROUND_TASK);
	if (ret != 0)
		bail_out("could not become regular task (huh?)");

	reservation_destroy(gettid(), config.cpu);
	//test_call(0);
	printf("#Done. Dropped %d frames.\n", num_dropped_frames);
	CloseWebcam(&webcam);
	free(rgba_buffer);

	//for ( i = 0; i < result_index; i++)
		//printf("%ld,%ld\n", result[i].t1, result[i].t2);
	return 0;
}

