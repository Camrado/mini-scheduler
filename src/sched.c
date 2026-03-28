#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sched.h"
#include "trace.h"
#include "process.h"
#include "cpu.h"

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

/* =======================================================================
 * Heap-based scheduler infrastructure
 *
 * Uses the provided MinHeap / MaxHeap implementations from heap.h,
 * min_heap.c, and max_heap.c.
 *
 * process_t (defined in heap.h) is the element type for both heaps:
 *   .pid      = workload[] index  (same meaning in both heaps)
 *   .priority = ts   for arrival_heap  (MinHeap orders smallest-ts first)
 *   .priority = prio for ready_heap    (MaxHeap orders highest-prio first)
 *
 * We allocate two flat pools of process_t objects — one per heap — so
 * that the two heaps can order the same logical process on different keys
 * without aliasing problems.
 *
 * Two heaps drive the scheduler:
 *
 *   arrival_heap (MinHeap, key = ts)
 *     Holds processes that have not yet reached their start time.
 *     Entries are promoted to ready_heap when ts_i <= t.
 *
 *   ready_heap (MaxHeap, key = prio)
 *     Holds every process with ts_i <= t that may still run.
 *     tf_i >= t is checked lazily on pop (tf can only grow, never shrink,
 *     so a truly finished process stays finished).
 *
 * overflow[] is a plain array of process_t* used as a temporary staging
 * area for processes popped from ready_heap that did not fit in the CPU
 * this round.  They are re-inserted into ready_heap after the fill loop.
 * ======================================================================= */
#include "heap.h"


void time_loop(size_t workload_size, size_t ts, size_t tf,
               size_t ncpus, pstate **timeline) {
    (void)ncpus;

    process_t *arrival_pool = NULL;
    process_t *ready_pool   = NULL;
    process   *run          = NULL;
    process   *pend         = NULL;
    process_t **overflow    = NULL;
    process_t **ran_this_tick = NULL;

    MinHeap arrival_heap = {0};
    MaxHeap ready_heap   = {0};

    arrival_pool = malloc(sizeof(process_t) * workload_size);
    ready_pool   = malloc(sizeof(process_t) * workload_size);
    run          = malloc(sizeof(process) * workload_size);
    pend         = malloc(sizeof(process) * workload_size);
    overflow     = malloc(sizeof(process_t *) * workload_size);
    ran_this_tick = malloc(sizeof(process_t *) * workload_size);

    if (!arrival_pool || !ready_pool || !run || !pend || !overflow || !ran_this_tick) {
        perror("time_loop: malloc");
        goto cleanup;
    }

    minheap_init(&arrival_heap, workload_size);
    maxheap_init(&ready_heap, workload_size);

    for (size_t i = 0; i < workload_size; i++) {
        arrival_pool[i].pid      = (int)i;
        arrival_pool[i].priority = (int)workload[i].ts;
        minheap_insert(&arrival_heap, &arrival_pool[i]);

        ready_pool[i].pid      = (int)i;
        ready_pool[i].priority = workload[i].prio;
    }

    for (size_t t = ts; t <= tf; t++) {
        /* Step 1: promote newly arrived processes */
        while (!minheap_empty(&arrival_heap)) {
            process_t *top = minheap_top(&arrival_heap);
            size_t idx = (size_t)top->pid;

            if (workload[idx].ts > t) {
                break;
            }

            minheap_pop(&arrival_heap);
            maxheap_insert(&ready_heap, &ready_pool[idx]);
        }

        /* Step 2: lazily discard finished processes at heap top */
        while (!maxheap_empty(&ready_heap)) {
            process_t *top = maxheap_top(&ready_heap);
            size_t idx = (size_t)top->pid;

            if (workload[idx].tf >= t) {
                break;
            }

            maxheap_pop(&ready_heap);
        }

        size_t nb_run = 0;
        size_t nb_overflow = 0;
        size_t nb_ran = 0;
        size_t nb_pend = 0;
        int prio_sum = 0;
        bool cap_reached = false;

        /* Step 3: greedy CPU fill */
        while (!maxheap_empty(&ready_heap)) {
            process_t *proc = maxheap_top(&ready_heap);
            size_t idx = (size_t)proc->pid;
            maxheap_pop(&ready_heap);

            /* lazy discard if not at top during previous cleanup */
            if (workload[idx].tf < t) {
                continue;
            }

            if (!cap_reached && prio_sum + workload[idx].prio <= CPU_CAPABILITY) {
                run[nb_run].prio = workload[idx].prio;
                run[nb_run].pid  = idx;
                nb_run++;

                ran_this_tick[nb_ran++] = proc;
                prio_sum += workload[idx].prio;
            } else {
                cap_reached = true;
                overflow[nb_overflow++] = proc;
            }
        }

        /* Step 4: apply idle/tf penalty only to already-started pending processes */
        for (size_t o = 0; o < nb_overflow; o++) {
            size_t idx = (size_t)overflow[o]->pid;

            if (workload[idx].ts < t && t <= workload[idx].tf) {
                workload[idx].idle++;
                workload[idx].tf++;
            }
        }

        /* Step 5: build pending array for timeline recording */

        /* pending current processes that did not fit */
        for (size_t o = 0; o < nb_overflow; o++) {
            size_t idx = (size_t)overflow[o]->pid;

            if (t <= workload[idx].tf) {
                pend[nb_pend].prio = workload[idx].prio;
                pend[nb_pend].pid  = idx;
                nb_pend++;
            }
        }

        /* future processes still waiting to start */
        for (size_t a = 1; a <= arrival_heap.size; a++) {
            size_t idx = (size_t)arrival_heap.array[a]->pid;

            pend[nb_pend].prio = workload[idx].prio;
            pend[nb_pend].pid  = idx;
            nb_pend++;
        }

        record_timeline(t, tf + 1,
                        timeline,
                        run, nb_run,
                        pend, nb_pend,
                        workload_size);

        printf("  [t=%zu] run(%zu):", t, nb_run);
        for (size_t r = 0; r < nb_run; r++) {
            printf(" (%d,pid=%zu)", run[r].prio, run[r].pid);
        }
        printf("  pend(%zu):", nb_pend);
        for (size_t p = 0; p < nb_pend; p++) {
            printf(" (%d,pid=%zu)", pend[p].prio, pend[p].pid);
        }
        printf("\n");

        /* Step 6: reinsert live running processes for future ticks */
        for (size_t r = 0; r < nb_ran; r++) {
            size_t idx = (size_t)ran_this_tick[r]->pid;

            if (workload[idx].tf > t) {
                maxheap_insert(&ready_heap, ran_this_tick[r]);
            }
        }

        /* Step 7: reinsert pending live processes */
        for (size_t o = 0; o < nb_overflow; o++) {
            size_t idx = (size_t)overflow[o]->pid;

            if (workload[idx].tf >= t) {
                maxheap_insert(&ready_heap, overflow[o]);
            }
        }
    }

cleanup:
    minheap_free(&arrival_heap);
    maxheap_free(&ready_heap);
    free(arrival_pool);
    free(ready_pool);
    free(run);
    free(pend);
    free(overflow);
    free(ran_this_tick);
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