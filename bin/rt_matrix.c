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

#define MIN_SEED -2147483647
#define MAX_SEED -1
#define MIN_DIM  1
#define MAX_DIM  32768
#define MAX_ITERATIONS 65536
#define MIN_TOLERANCE 0.000007
#define MAX_TOLERANCE 0.5
#define MIN_NUMBER   -3.4e10/dim
#define MAX_NUMBER 3.4e10/dim
#define EPSI   1.0e-10
#define MIN_DIG_NUMBER 1.0e-10
#define MAX_DIG_NUMBER 3.4e10

int loops = 1;

/*
 *  External variable, dimension
 */

int max_dim;
double *vectorP, *vectorR, *nextVectorR;
double  *matrixA = NULL;
double  *vectorB = NULL;
double  *vectorX = NULL;
double *value = NULL;
int    *col_ind = NULL;
int    *row_start = NULL;
double  *tmpVector1, *tmpVector2, *tmpVector3;

/*      
 *  matrix * vector     
 */

void matrixMulvector(double *value, 
		     int *col_ind, 
		     int *row_start,
		     double  *vector,
		     double *out,
		     int dim)
{  
  int l, ll;
  int tmp_rs, tmp_re;
 
  for (l=0; l<dim; l++){  
     *(out + l) = 0;
     tmp_rs = row_start[l];
    
     if (tmp_rs != -1){
      tmp_re = row_start[l+1];   /*
				  *get the start and ending elements of 
				  *  each row
				 */
      for (ll=tmp_rs; ll<tmp_re; ll++){
	*(out + l) += value[ll]*vector[col_ind[ll]];
      }
    }
  }
  return; 
}


/*
 *    vector1 - vector2
 */

void  vectorSub(double  *vector1, double  *vector2, double *vector, int dim){

  int l;

  for (l=0; l<dim; l++){
    *(vector + l) = *(vector1 + l) - *(vector2 + l);
  }
  return; 
}


/*
 * vector1 + vector2
 */

void vectorAdd(double  *vector1, double  *vector2, double *vector, int dim){

  int l;

  for (l=0; l<dim; l++){
    *(vector + l) = *(vector1 + l) + *(vector2 + l);
  }
  return; 
} 

/* 
 * vector1 * vector2
 */

double  vectorMul(double  *vector1, double  *vector2, int dim){

  int l;
  double  product;

  product = 0;

  for (l=0; l<dim; l++){
    product += (*(vector1 + l))*(*(vector2 + l));

  }
  return product;
} 

/*
 * /vector/
 */

double  vectorValue(double  *vector, int dim){

  double  value;
  int l;

  value = 0;

  for (l=0; l<dim; l++){
    value += (*(vector + l)) * (*(vector + l));
  }

  return (sqrt(value));
}

/*
 * transpose(vector)
 * In fact, we return the original vector here
 */

void  transpose(double  *vector, double *vect){

  int l;

  for (l=0; l<max_dim; l++){
    *(vect+l) = *(vector+l);
  }
  return; 
}

/*
 * value * <vector>
 */
void valueMulvector(double  value, double  *vector, double *vect){

  int l;

  for (l=0; l<max_dim; l++){
    *(vect + l) = *(vector + l) * value;
  }
  return;
}
  
/*
 * generate the data distributed sparsely in matrix
 */

void initMatrix(double  *matrix, int dim, int numberNonzero){
  
  int k, l, ll;
  int i, j;

  int lll;
  double sum;

  for (k=0; k< dim*dim; k++){
    *(matrix + k) = 0;
  }

  for (l=0; l<numberNonzero/2; l++){

    i = randomUInt(1, dim-1);
    j = randomUInt(0, i-1);

    while (*(matrix + i*dim + j) != 0){
      
     i++;
       if (i == dim){
       j++;
       if (j == dim-1){
	 j = 0;
	 i = 1;
       }
       else{
	 i = j+1;
       }
     }
    }
  
    if (*(matrix + i*dim + j) == 0){
      *(matrix + i*dim + j) = (double )randomNonZeroFloat(MIN_NUMBER, 
							  MAX_NUMBER, 
							  EPSI);
      *(matrix + j*dim + i) = *(matrix + i*dim + j);
    }
  }
 
  for (ll=0; ll<dim; ll++){


    
    *(matrix + ll*dim + ll) = (double )randomNonZeroFloat(-MAX_DIG_NUMBER,
							  MAX_DIG_NUMBER, 
							  MIN_DIG_NUMBER);
    
    sum = 0;

    for (lll=0; lll<dim; lll++){
      if (lll != ll){
	sum += *(matrix + lll*dim + ll);
      }
    }
    
    if (*(matrix + ll*dim + ll) < sum ){
      *(matrix + ll*dim + ll) += sum;
   }
  }

  return;
}

/*
 * generate the data value in the vectors
 */
 
void initVector(double *vector, int dim){

  int l;
  
  for (l=0; l<dim; l++){
    *(vector + l) = (double )randomFloat (MIN_NUMBER, MAX_NUMBER);
  }

  return;
}

/*
 * make a vector contains value of zero
 */

void zeroVector(double *vector, int dim){
  int l;
  
  for (l=0; l<dim; l++){
    *(vector + l) = 0;
  }
  return;
}

/*
 * return a vector which is the copy of the vect
 */

void equalVector(double *vect, double *vect1){

  int l;

  for (l=0; l<max_dim; l++){
    *(vect1+l) = *(vect+l);
  }
  return; 
}



void biConjugateGradient(double *value,
			 int *col_ind,
			 int *row_start,
			 double *vectorB, 
			 double *vectorX,
			 double errorTolerance,
			 int maxIterations,
			 double *actualError,
			 int *actualIteration,
			 int dim)
     /* 
      * in the code, we use a lot of temparary vectors and variables
      * this is just for simple and clear
      * you can optimize these temporary variables and vectors 
      * based on your need
      *
      */
{
  double  error;
  int iteration;
  double  alpha, beta;

  double   tmpValue1, tmpValue2; 
  int l;
  int ll;

  alpha = 0;
  beta = 0;

  /*
   * vectorR = vectorB - matrixA*vectorX
   */
  matrixMulvector(value,col_ind, row_start, vectorX, tmpVector1, dim);

  vectorSub(vectorB, tmpVector1, vectorR, dim);

  /*
   * vectorP = vectorR
   */

  equalVector(vectorR, vectorP);

  /*
   * error = |matrixA * vectorX - vectorB| / |vectorB|
   */
  vectorSub(tmpVector1, vectorB, tmpVector1, dim); 

  error = vectorValue(tmpVector1,dim)/vectorValue(vectorB,dim);

  iteration = 0;

  while ((iteration < maxIterations) && (error > errorTolerance)){
   
    /* 
     *   alpha = (transpose(vectorR) * vectorR) /
     *           (transpose(vectorP) * (matrixA * vectorP)
     */

    matrixMulvector(value, col_ind, row_start, vectorP, tmpVector1, dim);  
    transpose(vectorR, tmpVector2); 
    transpose(vectorP, tmpVector3);
    tmpValue1 = vectorMul(tmpVector3, tmpVector1, dim);
    tmpValue2 = vectorMul(tmpVector2, vectorR, dim);
    alpha = tmpValue2/tmpValue1;
 
    /* 
     * nextVectorR = vectorR - alpha*(matrixA * vectorP)
     */

    valueMulvector(alpha, tmpVector1, tmpVector2);
    vectorSub(vectorR, tmpVector2, tmpVector1, dim);
    equalVector(tmpVector1, nextVectorR);
 
    /* 
     * beta = (transpose(nextVectorR) * nextVectorR) /
     *           (transpose(vectorR) * vectorR)
     */

    transpose(nextVectorR, tmpVector3);
    tmpValue1 = vectorMul(tmpVector3, nextVectorR, dim);
    transpose(vectorR, tmpVector2);
    tmpValue2 = vectorMul(tmpVector2, vectorR, dim);
    beta = tmpValue1/tmpValue2;

    /*
     * vectorX = vectorX + alpha * vectorP
     */
    valueMulvector(alpha, vectorP, tmpVector1);       
    vectorAdd(vectorX,tmpVector1, vectorX, dim);

    /* 
     *vectorP = nextVectorR + beta*vectorP
     */       
    valueMulvector(beta, vectorP, tmpVector1);   
    vectorAdd(nextVectorR, tmpVector1, tmpVector1, dim);

    for (ll=0; ll<dim; ll++){
      *(vectorP + ll) = *(tmpVector1 + ll);
    }

    /*
     * vectorR = nextVectorR
     */
    
    for (l=0; l<dim; l++){
    *(vectorR+l) = *(nextVectorR+l);
    }

    /* 
     * error = |matrixA * vectorX - vectorB| / |vectorB|
     */
    matrixMulvector(value, col_ind,row_start, vectorX, tmpVector1, dim);
    vectorSub(tmpVector1,vectorB,tmpVector1,dim);
    error = vectorValue(tmpVector1,dim)/vectorValue(vectorB,dim);

    iteration++;
  }

  *actualError = error;
  *actualIteration = iteration;


  return;
}
  
/*
 * This is the function to transfer the data from the matrix of dense storage 
 * to Compact Row Storage
 */
void create_CRS(double *matrixA,
		double *value, 
		int *col_ind, 
		int *row_start,
		int dim,
		int numberNonzero)
{

  int i, j, k;
  int cnt;
  double tmp;

  /* 
   *initialize the row_start
   */
     
  for(k=0; k<dim; k++){
    row_start[k] = -1;
  }
  
  /* 
   * make the end of the last row to be numberNonzero + dim.
   */

  row_start[dim] = numberNonzero+dim;
  
  /*
   * initialize the col_ind
   */

  for (k=0; k<numberNonzero+dim; k++){
    col_ind[k] = -1;
  }


  cnt = 0;

  for (i=0;  (cnt<numberNonzero+dim)&&(i<dim); i++){
    for (j=0; (cnt<numberNonzero+dim)&&(j<dim); j++){
      
      tmp = *(matrixA + i*dim + j);

         if (tmp!=0){

	   value[cnt] = tmp;
	   col_ind[cnt] = j;
       
	    if (row_start[i] == -1)
	      row_start[i] = cnt;
	    
	    cnt += 1;
	 }	
    }
  }
  row_start[i] = cnt;

  return;
}

  int seed;
  int max_numberNonzero;
  int maxIterations;
  float errorTolerance;
  int k;

int init_job() {
  
  max_dim = 200;
  max_numberNonzero = 300;
  
  errorTolerance = 0.02734;
  
  assert((max_dim > MIN_DIM) && (max_dim < MAX_DIM));
  assert((max_numberNonzero > max_dim) && (max_numberNonzero < max_dim*max_dim));
  assert((errorTolerance > MIN_TOLERANCE) && (errorTolerance < MAX_TOLERANCE));
  
  matrixA = (double  *)malloc(max_dim*max_dim*sizeof(double ));
  vectorB = (double *)malloc(max_dim*sizeof(double));
  vectorX = (double *)malloc(max_dim*sizeof(double));

  value = (double *)malloc((max_numberNonzero+max_dim)*sizeof(double));
  col_ind = (int *)malloc((max_numberNonzero+max_dim)*sizeof(int));
  row_start = (int *)malloc((max_dim+1)*sizeof(int));
  
  vectorP = (double *)malloc(max_dim*sizeof(double));
  vectorR = (double *)malloc(max_dim*sizeof(double));
  nextVectorR = (double *)malloc(max_dim*sizeof(double));
  
  tmpVector1 = (double *)malloc(max_dim*sizeof(double));
  tmpVector2 = (double *)malloc(max_dim*sizeof(double));
  tmpVector3 = (double *)malloc(max_dim*sizeof(double));

  return 0;
}

int main_job() {
  int sum, dim, numberNonzero;
  double  actualError;
  int actualIteration;
  
  dim = 200; //1500; //200
  numberNonzero = 300; //1600; //300
  maxIterations = 100;
  
  
  initMatrix(matrixA, dim, numberNonzero);
  create_CRS(matrixA, value, col_ind, row_start, dim, numberNonzero);

  initVector(vectorB, dim);
  zeroVector(vectorX, dim);

  actualError = 0;
  actualIteration = 0;

  biConjugateGradient(value, col_ind, row_start, vectorB, vectorX, errorTolerance,
		      maxIterations,
		      &actualError, &actualIteration, dim);

  sum = 0;
  for (k=1; k<dim; k++){
    sum += sum + *(vectorX + k);
  }
  
  return(0);
}

int post_job() {
	if (matrixA) {
		free(matrixA);
		matrixA = NULL;
	}
	if (vectorB) {
		free(vectorB);
		vectorB = NULL;
	}
	if (vectorX) {
		free(vectorX);
		vectorX = NULL;
	}
	if (value) {
		free(value);
		value = NULL;
	}
	if (col_ind) {
		free(col_ind);
		col_ind = NULL;
	}
	if (row_start) {
		free(row_start);
		row_start = NULL;
	}
	if (vectorP) {
		free(vectorP);
		vectorP = NULL;
	}
	if (vectorR) {
		free(vectorR);
		vectorR = NULL;
	}
	if (nextVectorR) {
		free(nextVectorR);
		nextVectorR = NULL;
	} 
	if (tmpVector1) {
		free(tmpVector1);
		tmpVector1 = NULL;
	} 
	if (tmpVector2) {
		free(tmpVector2);
		tmpVector2 = NULL;
	} 
	if (tmpVector3) {
		free(tmpVector3);
		tmpVector3 = NULL;
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
		//register cycles_t t;
		//t = get_cycles();
		while (iter++ < loops) {
			main_job();
		}
		//t = get_cycles() - t;
		//printf("%ld cycles\n", t);
		//result[result_index++] = t;
		sleep_next_period();
		return 1;
	}
}

#define OPTSTR "p:wvk:m:i:b:u:l:"
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
	//for (i = 0; i < result_index; i++)
		//printf("%ld\n", result[i]);
	return 0;
}
