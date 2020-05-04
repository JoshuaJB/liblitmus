// This is a simple program which will display webcam video in a window.
//
// Usage:
//    ./sdl_camera <device path e.g. "/dev/video0">
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <unistd.h>
#include "webcam_lib_old.h"
#include <sys/time.h>
#include <sys/mman.h>
#include <limits.h>

#include "litmus.h"
#include "common.h"

// The number of webcam resolutions to enumerate when checking resolutions.
#define MAX_RESOLUTION_COUNT (30)

static struct {
  WebcamInfo webcam;
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  uint32_t w;
  uint32_t h;
} g;

static char* ErrorString(void) {
  return strerror(errno);
}

static void CleanupSDL(void) {
  if (g.renderer) {
    SDL_DestroyRenderer(g.renderer);
    g.renderer = NULL;
  }
  if (g.window) {
    SDL_DestroyWindow(g.window);
    g.window = NULL;
  }
  if (g.texture) {
    SDL_DestroyTexture(g.texture);
    g.texture = NULL;
  }
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

// Enumerates the resolutions provided by the webcam and selects a suitable
// width and height. To be called during SetupWebcam.
static void SelectResolution(int resol) {
  WebcamResolution resolutions[MAX_RESOLUTION_COUNT];
  WebcamInfo *webcam = &(g.webcam);
  int selected_index = -1;
  int selected_size = 0x7fffffff;
  int current_size, i;
  memset(resolutions, 0, sizeof(resolutions));
  if (!GetSupportedResolutions(webcam, resolutions, MAX_RESOLUTION_COUNT)) {
    printf("Error getting supported resolutions: %s\n", ErrorString());
    CloseWebcam(webcam);
    exit(1);
  }
  for (i = 0; i < MAX_RESOLUTION_COUNT; i++) {
    current_size = resolutions[i].width * resolutions[i].height;
    printf("Resolution %dx%d index = %d\n", resolutions[i].width, resolutions[i].height, i);
    if (current_size == 0) break;
    if (current_size > selected_size) continue;
    selected_size = current_size;
    selected_index = i;
  }
  if (selected_index < 0) {
    printf("Error: Found no valid resolutions.\n");
    CloseWebcam(webcam);
    exit(1);
  }

  //selected_index = resol;
  selected_index = 1;
  g.w = resolutions[selected_index].width;
  g.h = resolutions[selected_index].height;
}

// Initializes the webcam struct. Exits on error.
static void SetupWebcam(char *path, int resol) {
  WebcamInfo *webcam = &(g.webcam);
  if (!OpenWebcam(path, webcam)) {
    printf("Error opening webcam: %s\n", ErrorString());
    exit(1);
  }
  if (!PrintCapabilityDetails(webcam)) {
    printf("Error printing camera capabilities: %s\n", ErrorString());
    exit(1);
  }
  if (!PrintVideoFormatDetails(webcam)) {
    printf("Error printing video format details: %s\n", ErrorString());
    goto error_exit;
  }
  SelectResolution(resol);
  if (!SetResolution(webcam, g.w, g.h)) {
    printf("Error setting video resolution: %s\n", ErrorString());
    goto error_exit;
  }
  return;
error_exit:
  CloseWebcam(webcam);
  exit(1);
}

// Once the webcam has been opened, call this to set up the SDL info necessary
// for displaying the image.
static void SetupSDL(void) {
  WebcamInfo *webcam = &(g.webcam);
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL error: %s\n", SDL_GetError());
    goto error_exit;
  }
 
  g.window = SDL_CreateWindow("Webcam view", SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED, g.w, g.h, SDL_WINDOW_SHOWN |
    SDL_WINDOW_RESIZABLE);
  if (!g.window) {
    printf("SDL error creating window: %s\n", SDL_GetError());
    goto error_exit;
  }
  g.renderer = SDL_CreateRenderer(g.window, -1, SDL_RENDERER_ACCELERATED);
  if (!g.renderer) {
    printf("Failed creating SDL renderer: %s\n", SDL_GetError());
    goto error_exit;
  }

  g.texture = SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_RGBA8888,
    SDL_TEXTUREACCESS_STREAMING, g.w, g.h);
  if (!g.texture) {
    printf("Failed getting SDL texture: %s\n", SDL_GetError());
    goto error_exit;
  }

  return;
error_exit:
  CloseWebcam(webcam);
  CleanupSDL();
  exit(1);
}



// Copy images from the camera to the window, until an SDL quit event is
// detected.
SDL_Event event;
size_t frame_size = 0;
void *frame_bytes = NULL;
void *texture_pixels = NULL;
int texture_pitch = 0;
int quit = 0;
WebcamInfo *webcam = &(g.webcam);

static void loop_once(void) {
    // Start loading a new frame from the webcam.
    if (!BeginLoadingNextFrame(webcam)) {
      printf("Error getting webcam frame: %s\n", ErrorString());
      goto error_exit;
    }
    // Sleep for 10 ms while the frame gets loaded.
    //usleep(1000);

    // Lock the texture in order to modify its pixel data directly.
    if (SDL_LockTexture(g.texture, NULL, &texture_pixels, &texture_pitch)
      < 0) {
      printf("Error locking SDL texture: %s\n", SDL_GetError());
      goto error_exit;
    }
	
    // By this point, the image data should have finished loading, so get a
    // pointer to its location.
    if (!GetFrameBuffer(webcam, &frame_bytes, &frame_size)) {
      printf("Error getting frame from webcam: %s\n", ErrorString());
      goto error_exit;
    }
    if (!ConvertYUYVToRGBA(frame_bytes, texture_pixels, g.w, g.h, g.w * 2,
      texture_pitch)) {
      printf("Failed converting YUYV to RGBA color.\n");
      goto error_exit;
    }
	
    // Finalize the texture changes, re-draw the texture, re-draw the window
    SDL_UnlockTexture(g.texture);

    if (SDL_RenderCopy(g.renderer, g.texture, NULL, NULL) < 0) {
      printf("Error rendering texture: %s\n", SDL_GetError());
      goto error_exit;
    }
    SDL_RenderPresent(g.renderer);

	return;
  
error_exit:
	CloseWebcam(webcam);
	CleanupSDL();
	exit(1);
}

static int job(double exec_time, double program_end)
{
	if (wctime() > program_end)
		return 0;
	else {
		register cycles_t t;
		t = get_cycles();
		loop_once();
		t = get_cycles() - t;
		printf("%ld cycles\n", t);
   		sleep_next_period(); 
    	return 1;
	}
}

#define OPTSTR "p:wm:i:b:r:d:"
int main(int argc, char **argv) {
	int res = 0;
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
	char dev_name[255];

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
		case 'r':
			res = atoi(optarg);
			break;
		case 'd':
			sprintf(dev_name, "%s", optarg);
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
	
	memset(&g, 0, sizeof(g));
	
	ret = init_litmus();
	if (ret != 0)
		bail_out("init_litmus() failed\n");

	SetupWebcam(dev_name, res);
	SetupSDL();
	printf("Showing %dx%d video.\n", (int) g.w, (int) g.h);
	loop_once();
/*	
	if (mc2_param.crit == CRIT_LEVEL_C)
		set_page_color(-1);
	else
		set_page_color(config.cpu);
*/
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
	
	CloseWebcam(&(g.webcam));
	CleanupSDL();
	return 0;
}
