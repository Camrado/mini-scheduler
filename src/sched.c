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

/* -----------------------------------------------------------------------
 * Helpers for time_loop
 * ----------------------------------------------------------------------- */

/**
 * @brief Comparison function for qsort: sorts process array by descending
 *        priority so we can greedily fill the CPU from highest prio down.
 */
static int cmp_prio_desc(const void *a, const void *b) {
    const process *pa = (const process *)a;
    const process *pb = (const process *)b;
    /* higher prio first */
    return pb->prio - pa->prio;
}

/**
 * @brief Determine whether process i was in the run queue at the previous
 *        timestep, by checking if the timeline recorded it as 'running'.
 *
 * @param i          workload index of the process
 * @param t          current timestep (>= 1)
 * @param timeline   the 2-D pstate array [timestep][process_index]
 * @return true if process i was running at t-1
 */
static bool was_running(size_t i, size_t t, pstate **timeline) {
    if (t == 0) return false;
    return timeline[i][t - 1] == running;
}

/* -----------------------------------------------------------------------
 * time_loop
 *
 * Simulates the scheduler from timestep ts to tf (inclusive).
 *
 * Algorithm at each timestep t:
 *   1. Collect "current" processes: ts_i <= t <= tf_i  (alive and started)
 *   2. Sort them by priority descending.
 *   3. Greedily assign to run queue while sum(prio) <= CPU_CAPABILITY.
 *      The remaining current processes go to the pending (wait) queue.
 *   4. Any current process that was running at t-1 but is now in the
 *      pending queue has been de-scheduled: increment its idle counter
 *      and its tf (finish time is pushed back by 1).
 *   5. Record the state of every process in the timeline:
 *        - finished (tf_i < t)  -> recorded later by print_timeline as '_'
 *        - running              -> 'R'
 *        - pending/waiting      -> '.'
 *      We call record_timeline() which handles this via the run/pend arrays.
 * ----------------------------------------------------------------------- */
void time_loop(size_t workload_size, size_t ts, size_t tf, size_t ncpus, pstate **timeline) {
    (void)ncpus;

    process *run  = malloc(sizeof(process) * workload_size);
    process *pend = malloc(sizeof(process) * workload_size);

    if (!run || !pend) {
        perror("time_loop: malloc");
        free(run);
        free(pend);
        return;
    }

    for (size_t t = ts; t <= tf; t++) {
        size_t nb_run  = 0;
        size_t nb_pend = 0;

        process *candidates = malloc(sizeof(process) * workload_size);
        size_t nb_cand = 0;

        if (!candidates) {
            perror("time_loop: malloc");
            free(run);
            free(pend);
            return;
        }

        /* Step 1: collect current processes only */
        for (size_t i = 0; i < workload_size; i++) {
            if (workload[i].ts <= t && t <= workload[i].tf) {
                candidates[nb_cand].prio = workload[i].prio;
                candidates[nb_cand].pid  = i;
                nb_cand++;
            }
        }

        /* Step 2: sort current processes by descending priority */
        qsort(candidates, nb_cand, sizeof(process), cmp_prio_desc);

        /* Step 3: greedily fill running queue up to CPU capability */
        int prio_sum = 0;
        for (size_t c = 0; c < nb_cand; c++) {
            int p = candidates[c].prio;
            if (prio_sum + p <= CPU_CAPABILITY) {
                run[nb_run++] = candidates[c];
                prio_sum += p;
            } else {
                pend[nb_pend++] = candidates[c];
            }
        }

        /* Step 4: future processes are also pending */
        for (size_t i = 0; i < workload_size; i++) {
            if (t < workload[i].ts) {
                pend[nb_pend].prio = workload[i].prio;
                pend[nb_pend].pid  = i;
                nb_pend++;
            }
        }

        free(candidates);

		/* Step 5: every already-started process that is pending at this timestep
		   loses one time unit and therefore gets idle++ and tf++ */
		for (size_t j = 0; j < nb_pend; j++) {
		    size_t idx = pend[j].pid;
		
		    /* future processes are pending but should not be penalized yet */
		    if (workload[idx].ts < t && t <= workload[idx].tf) {
		        workload[idx].idle++;
		        workload[idx].tf++;
		    }
		}

        /* Step 6: record timeline */
        record_timeline(t, tf + 1,
                        timeline,
                        run,  nb_run,
                        pend, nb_pend,
                        workload_size);

        printf("  [t=%zu] run(%zu):", t, nb_run);
        for (size_t r = 0; r < nb_run; r++)
            printf(" (%d,pid=%zu)", run[r].prio, run[r].pid);

        printf("  pend(%zu):", nb_pend);
        for (size_t p = 0; p < nb_pend; p++)
            printf(" (%d,pid=%zu)", pend[p].prio, pend[p].pid);

        printf("\n");
    }

    free(run);
    free(pend);
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