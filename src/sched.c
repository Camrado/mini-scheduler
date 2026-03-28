#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sched.h"
#include "trace.h"
#include "process.h"
#include "cpu.h"
#include "heap.h"

#define END_STEP 30

struct workload_item_t {
	int pid;       //< the event id
    int ppid;      //< the event parent id
	size_t ts;     //< start date
	size_t tf;     //< finish date
	size_t idle;   //< total time the process has been idle;
	char *cmd;     //< the binary name
	int prio;      //< process priority
};

enum  epoch { before, in, after };  //< to compare a date and a timeframe 
typedef enum epoch epoch;


   /**
    *            0,init
    *         /           \
	*      1,bash          2,bash
	*     /   \   \        /      \ 
	* 3,find   \   \      |       |
	*         4,gcc \     |       |
	*            |   |    |       |
	*	       5,ld  |    |       | 
	*                |    6,ssh   |
	*                |    |       |
	*                |    7,crypt |
	*                |           8,snake
    *               9,cat
    */

   /*
	workload_item workload[] = {
	//  pid ppid  ts  tf idle  cmd     prio
	    {0, -1,    0, 18,  0, "init",  10 },
        {1,  0,    1, 16,  0, "bash",   1 },
        {2,  0,    3, 16,  0, "bash",   1 },
        {3,  1,    4,  6,  0, "find",   2 },
        {4,  1,    7,  9,  0, "gcc",    5 },
		{5,  4,    8,  9,  0, "ld",     4 }, 
		{6,  2,   10, 13,  0, "ssh",    3 },
        {7,  6,   11, 13,  0, "crypt",  5 },
        {8,  2,   14, 16,  0, "snake",  4 },
        {9,  1,   14, 15,  0, "cat",    5 },
	};
	*/
void draw_hbar(char c, size_t width) {
	char bar[width+1];
	memset(bar, c, width);
	bar[width]='\0';
	printf("%s",bar);
}

void chronogram(workload_item* workload, size_t nb_processes, size_t timesteps) {
	// drw timeliine
	size_t tick;
	size_t freq=5;
	printf("\t");
	for (tick=0; tick<timesteps; tick++) {
		if (tick%freq==0) printf("|"); else printf(".");
	}
	printf("\n");
	// draw processes lifetime
	for (size_t i=0; i<nb_processes; i++) {
		printf("%s\t", workload[i].cmd);
		draw_hbar(' ',workload[i].ts);
		draw_hbar('X',workload[i].tf-workload[i].ts);
		printf("\t\t\t (tf=%zu,idle=%zu)\n", workload[i].tf, workload[i].idle);
	}
}


/**
 * @brief count lines in file
 * 
 * @param file assumed to be an open file
 * @return the number of lines in files 
 */
size_t count_lines_in_file(FILE *file) {
    int lines = 0;
    char ch;
    // Count the number of newline characters
    while ((ch = fgetc(file)) != EOF) {
        if (ch == '\n') {
            lines++;
        }
    }
    // If the file is not empty and the last line doesn't end with '\n'
    if (ch != '\n' && lines != 0) {
        lines++;
    }
    rewind(file);
    return lines;
}

/**
 * @brief Read the workload data
 * 
 * @param filename the name of file, can be STDIN
 * @return number of data lines read in the file
 */
size_t read_data(size_t workload_size, FILE *file) {
    size_t count = 0;
    char line[256];  // Buffer for each line in the file
    char cmd[50];    // Buffer for command name

    while (fgets(line, sizeof(line), file) && count < workload_size) {
        workload_item item;
        // Parse the line into the workload_item structure
        if (sscanf(line, "%d %d %zu %zu %zu %s %d",
                   &item.pid, &item.ppid, &item.ts, &item.tf,
                   &item.idle, cmd, &item.prio) == 7) {
            item.cmd = strdup(cmd);  // Duplicate the string for command name
            workload[count++] = item;
        } else {
            fprintf(stderr, "Error parsing line: %s\n", line);
			return false;
        }
	}
	return count;
}

/*
 * scan runningQueue and remove any process whose tf < t
 * we can't just iterate and pop because remove_at shifts elements,
 * so we only advance i when we didn't remove anything
 */
static void remove_finished_processes(Heap *runningQueue, size_t t,
                                      int *prioritySum) {
    size_t i = 0;
    while (i < runningQueue->size) {
        process_t *proc = runningQueue->arr[i];
        if (proc->tf < t) {
            printf("  > process pid=%d prio=%d ('%s') finished after t=%zu\n",
                   proc->pid, proc->priority, proc->cmd, t - 1);
            *prioritySum -= proc->priority;
            heap_remove_at(runningQueue, i);
            /* do NOT increment i - the slot was filled by the last element */
        } else {
            i++;
        }
    }
}
 
/* monotonically increasing counter stamped on each proc when it enters pendingQueue
 * this lets max_cmp use FIFO order as a tiebreaker */
static size_t g_sched_seq = 0;
 
/* put newly arrived procs (ts == t) into pendingQueue */
static void add_arrived_to_pending(Heap *pendingQueue,
                                   process_t *procs, size_t procCount,
                                   size_t t) {
    for (size_t i = 0; i < procCount; i++) {
        if (procs[i].ts == t) {
            procs[i].seq = g_sched_seq++;
            heap_insert(pendingQueue, &procs[i]);
            printf("  > pid=%d prio=%d ('%s') queued into pending\n",
                   procs[i].pid, procs[i].priority, procs[i].cmd);
        }
    }
}
 
/* bump idle counter for every proc still waiting in pendingQueue
 * tf was already extended in step 3 so we don't touch it here */
static void increment_idle_counters(Heap *pendingQueue) {
    for (size_t i = 0; i < pendingQueue->size; i++) {
        process_t *proc = pendingQueue->arr[i];
        proc->idle++;
        printf("  > idle pid=%d prio=%d ('%s') tf=%zu idle=%zu\n",
               proc->pid, proc->priority, proc->cmd, proc->tf, proc->idle);
    }
}
 
/*
 * build the run/pend arrays and call record_timeline
 * trace infrastructure expects these flat arrays, so we fill them from the heaps
 * also include procs that haven't arrived yet as pending (trace expects them)
 */
static void record_state(size_t t, size_t tf_limit,
                         pstate **timeline,
                         Heap *runningQueue, Heap *pendingQueue,
                         process_t *procs, size_t procCount,
                         size_t workload_size) {
    process *run  = malloc(sizeof(process) * runningQueue->size);
    process *pend = malloc(sizeof(process) * (pendingQueue->size + procCount));
 
    size_t nb_run  = 0;
    size_t nb_pend = 0;
 
    for (size_t i = 0; i < runningQueue->size; i++) {
        process_t *p = runningQueue->arr[i];
        if (p->tf >= t) {
            run[nb_run].pid  = p->pid;
            run[nb_run].prio = p->priority;
            nb_run++;
        }
    }
 
    for (size_t i = 0; i < pendingQueue->size; i++) {
        process_t *p = pendingQueue->arr[i];
        if (p->tf >= t) {
            pend[nb_pend].pid  = p->pid;
            pend[nb_pend].prio = p->priority;
            nb_pend++;
        }
    }
 
    /* future procs are still shown as pending in the timeline */
    for (size_t i = 0; i < procCount; i++) {
        if (procs[i].ts > t) {
            pend[nb_pend].pid  = procs[i].pid;
            pend[nb_pend].prio = procs[i].priority;
            nb_pend++;
        }
    }
 
    record_timeline(t, tf_limit, timeline,
                    run, nb_run,
                    pend, nb_pend,
                    workload_size);
 
    /* Console log for this tick */
    printf("  [t=%zu] run(%zu):", t, nb_run);
    for (size_t r = 0; r < nb_run; r++)
        printf(" (%d,pid=%d)", run[r].prio, run[r].pid);
    printf("  pend(%zu):", nb_pend);
    for (size_t p = 0; p < nb_pend; p++)
        printf(" (%d,pid=%d)", pend[p].prio, pend[p].pid);
    printf("\n");
 
    free(run);
    free(pend);
}
 
/*
 * main scheduling loop
 *
 * builds a process_t pool from workload[], runs two-queue scheduler
 * tick by tick, then writes tf/idle back for the chronogram
 */
void time_loop(size_t workload_size, size_t ts, size_t tf,
               size_t ncpus, pstate **timeline) {
    (void)ncpus; /* single logical CPU with capacity CPU_CAPABILITY */
 
    /* one process_t per workload entry - heaps point into this pool */
    process_t *procs = calloc(workload_size, sizeof(process_t));
    if (!procs) { perror("time_loop: calloc procs"); return; }
 
    for (size_t i = 0; i < workload_size; i++) {
        procs[i].pid      = workload[i].pid;
        procs[i].ppid     = workload[i].ppid;
        procs[i].ts       = workload[i].ts;
        procs[i].tf       = workload[i].tf;
        procs[i].idle     = workload[i].idle;
        procs[i].priority = workload[i].prio;
        /* safe copy of cmd (process_t.cmd is a fixed 32-char buffer) */
        strncpy(procs[i].cmd, workload[i].cmd, sizeof(procs[i].cmd) - 1);
        procs[i].cmd[sizeof(procs[i].cmd) - 1] = '\0';
    }
 
    /* both queues are the same Heap type, just different comparators */
    Heap runningQueue;
    Heap pendingQueue;
    heap_init(&runningQueue, workload_size, min_cmp);   /* weakest at top */
    heap_init(&pendingQueue, workload_size, max_cmp);   /* strongest at top */
 
    int prioritySum = 0; /* sum of priorities of currently running processes */
 
    for (size_t t = ts; t <= tf; t++) {
        printf("[t=%zu]\n", t);
 
        /* Step 1: evict finished processes from runningQueue */
        remove_finished_processes(&runningQueue, t, &prioritySum);
 
        /* Step 2: enqueue newly arrived processes into pendingQueue */
        add_arrived_to_pending(&pendingQueue, procs, workload_size, t);
 
        /*
         * Step 3: try to promote pending -> running
         *
         * pop best candidate from pendingQueue and check if it fits
         * if it fits under CPU_CAPABILITY: schedule it directly
         * if not: check if we can preempt the weakest running proc
         *   - candidate beats lowest runner -> preempt, then evict until under cap.
         *   - otherwise: bump candidate's tf and hold it for re-insertion.
         *
         * held-back procs go into holdQueue and are re-inserted after the loop
         * so they get a fresh seq number and don't mess up pending ordering
         */
        process_t **holdQueue = malloc(sizeof(process_t *) * pendingQueue.capacity + 1);
        size_t holdCount = 0;
 
        while (!heap_empty(&pendingQueue)) {
            process_t *candidate = heap_top(&pendingQueue);
 
            if (prioritySum + candidate->priority <= CPU_CAPABILITY) {
                /* Candidate fits - schedule it */
                heap_pop(&pendingQueue);
                printf("  > scheduled pid=%d prio=%d ('%s') pending -> running\n",
                       candidate->pid, candidate->priority, candidate->cmd);
                heap_insert(&runningQueue, candidate);
                prioritySum += candidate->priority;
                printf("  > CPU load: %d / %d\n", prioritySum, CPU_CAPABILITY);
 
            } else {
                /* Candidate doesn't fit - check for preemption */
                process_t *lowest = heap_top(&runningQueue);
 
                if (lowest != NULL && candidate->priority > lowest->priority) {
                    /* Preempt the lowest-priority running process */
                    heap_pop(&pendingQueue);
                    printf("  > scheduled pid=%d prio=%d ('%s') pending -> running (preempting)\n",
                           candidate->pid, candidate->priority, candidate->cmd);
                    heap_insert(&runningQueue, candidate);
                    prioritySum += candidate->priority;
 
                    /* Evict until we are back within capacity */
                    while (prioritySum > CPU_CAPABILITY && !heap_empty(&runningQueue)) {
                        process_t *evict = heap_top(&runningQueue);
                        heap_pop(&runningQueue);
                        prioritySum -= evict->priority;
 
                        printf("  > preempted pid=%d prio=%d ('%s') running -> pending\n",
                               evict->pid, evict->priority, evict->cmd);
 
                        evict->tf++;
                        holdQueue[holdCount++] = evict;
 
                        printf("  > CPU load: %d / %d\n", prioritySum, CPU_CAPABILITY);
                    }
 
                } else {
                    /* No preemption possible - candidate stays pending */
                    printf("  > pid=%d prio=%d ('%s') cannot fit, no preemption\n",
                           candidate->pid, candidate->priority, candidate->cmd);
                    heap_pop(&pendingQueue);
                    candidate->tf++;
                    holdQueue[holdCount++] = candidate;
                }
            }
        }
 
        /* put held-back procs back into pending with a fresh seq stamp */
        for (size_t i = 0; i < holdCount; i++) {
            holdQueue[i]->seq = g_sched_seq++;
            heap_insert(&pendingQueue, holdQueue[i]);
        }
        free(holdQueue);
 
        /* Step 4: increment idle counters for processes still pending */
        increment_idle_counters(&pendingQueue);
 
        /* Step 5: record timeline and print tick summary */
        record_state(t, tf + 1, timeline,
                     &runningQueue, &pendingQueue,
                     procs, workload_size, workload_size);
    }
 
    /* copy final tf/idle back so chronogram() can read them */
    for (size_t i = 0; i < workload_size; i++) {
        workload[i].tf   = procs[i].tf;
        workload[i].idle = procs[i].idle;
    }
 
    heap_free(&runningQueue);
    heap_free(&pendingQueue);
    free(procs);
}

/**
 * main
 */
int main(int argc, char** argv) {
	FILE *input;
	if (argc > 1) { // if one arg, use it to read in data 
		if ((input = fopen(argv[1],"r")) == NULL) {
			perror("Error reading file:");
			exit(EXIT_FAILURE);
		}
		else
			printf("* Read from %s ...", argv[1]);
	}
	else { // no arg provided, read from stdin
			printf("* Read from stdin ...");
			input = stdin;
	}
	// read from standard input
	fflush(stdout);
	size_t nr = count_lines_in_file(input);

	printf(" %zu lines in data.\n", nr);

	workload = malloc(sizeof(workload_item) * nr);
	size_t workload_size = read_data(nr, input);
	printf("* Loaded %zu lines of data.\n", nr);
	pstate **timeline = alloc_timeline(END_STEP, workload_size);

	if (nr > 0) 
		time_loop(workload_size, 0, END_STEP-1, MAX_CPU, timeline);
	else
		return EXIT_FAILURE;


	printf("* Chronogram === \n");
	chronogram(workload, workload_size, END_STEP-1);
	print_timeline(END_STEP-1, workload_size, timeline);
	free(workload);
	return 0;
}