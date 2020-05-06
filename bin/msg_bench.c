#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/mman.h>
#include "litmus.h"
#include "common.h"

//struct msgbuf {
//       long mtype;     /* message type, must be > 0 */
//       char mtext[1];  /* message data */
//};

#define MAX_SAMPLES	10000

int main(int argc, char* argv[])
{
    /* create a private message queue, with access only to the owner. */

    int queue_id = msgget(IPC_PRIVATE, 0600);
    struct msgbuf* msg;
    struct msgbuf* recv_msg;
    int rc, i, n;
    long mtype;
    int msg_size;
    cycles_t t, s_result[MAX_SAMPLES], r_result[MAX_SAMPLES];
    int s_index = 0, r_index = 0;
	cycles_t s_max = 0, r_max = 0;
	float s_avg = 0.0, r_avg = 0.0;
    int NUM_SAMPLES = 100;
	int temp[MAX_SAMPLES];

    for (i = 0 ; i<NUM_SAMPLES; i++) {
        s_result[i] = 0;
        r_result[i] = 0;
    }

    if (queue_id == -1) {
	perror("main: msgget");
	exit(1);
    }
    if (argc < 3) {
	printf("Usage %s <mtype> <msg_size> [num_samples]\n", argv[0]);
	exit(1);
    }
    else {
        mtype = atol(argv[1]);
        msg_size = atol(argv[2]);
        printf("type = %ld, size = %d\n", mtype, msg_size);
    }
    if (argc == 4)
        NUM_SAMPLES = atoi(argv[3]);

    for (n = 0 ; n < NUM_SAMPLES; n++) {
        //printf("message queue created, queue id '%d'.\n", queue_id);
        msg = (struct msgbuf*)malloc(sizeof(struct msgbuf)+msg_size);
        msg->mtype = mtype;
        for (i=0;i<msg_size;i++) {
            msg->mtext[i] = 'a';
        }

        t = get_cycles();
        rc = msgsnd(queue_id, msg, msg_size, 0);
        s_result[s_index++] = get_cycles() - t;

        if (rc == -1) {
            perror("main: msgsnd");
	    exit(1);
        }
        free(msg);
		/* do some thing here */
		for (i=0;i<rand()%MAX_SAMPLES;i++) {
			temp[i] = rand()%MAX_SAMPLES;
			temp[temp[i]] = rand()%MAX_SAMPLES;
		}
			
        //printf("message placed on the queue successfully. %ld\n", s_result[s_index-1]);
        recv_msg = (struct msgbuf*)malloc(sizeof(struct msgbuf)+msg_size);

        t = get_cycles();
        rc = msgrcv(queue_id, recv_msg, msg_size, 0, 0);
        r_result[r_index++] = get_cycles() - t;

        if (rc == -1) {
            perror("main: msgrcv");
	    exit(1);
        }

        //printf("msgrcv: received message: mtype '%ld'. %ld\n", recv_msg->mtype, r_result[r_index-1]);
    }
    
    for (i = 1 ; i<s_index; i++) {
		s_avg += (float)s_result[i];
		if (s_result[i]>s_max)
			s_max = s_result[i];
	}
	s_avg = s_avg / (s_index-1);
    for (i = 1 ; i<r_index; i++) {
		r_avg += (float)r_result[i];
		if (r_result[i]>r_max)
			r_max = r_result[i];
	}
	r_avg = r_avg / (r_index-1);
	
	printf("Send max: %lld, avg %f\n", s_max, s_avg);
	printf("Recv max: %lld, avg %f\n", r_max, r_avg);
    return 0;
}
