/**
 * @file sched.c
 *
 * Priority-based preemptive scheduler.
 *
 * Scheduling model (adapted from reference two-queue design):
 *
 *   runningQueue  (MinHeap, key = priority)
 *     Processes currently occupying CPU time this tick.
 *     MinHeap keeps the *weakest* running process at the top so it can be
 *     preempted in O(log n) when a higher-priority pending process needs room.
 *
 *   pendingQueue  (MaxHeap, key = priority)
 *     Processes that have arrived (ts <= t) but are not yet running.
 *     MaxHeap so the best candidate is always tried first.
 *
 * Per-tick algorithm:
 *   1. Remove finished processes from runningQueue (tf < t).
 *   2. Move newly arrived processes into pendingQueue (ts == t).
 *   3. Greedily promote pending -> running, with preemption:
 *        - If candidate fits (prioritySum + prio <= CPU_CAPABILITY): schedule it.
 *        - If it doesn't fit: try to preempt the lowest-priority running process.
 *          If the lowest runner has lower priority than the candidate, swap them.
 *          Otherwise leave candidate in pending and increment its idle/tf.
 *   4. Increment idle counters and tf for remaining pending processes.
 *   5. Record timeline state.
 *
 * Because process_t now carries all mutable state (ts, tf, idle, cmd, priority),
 * the heaps own the data directly — no stale-pointer hazards from workload[].
 * workload[] is kept for chronogram output (read-only after the loop).
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "sched.h"
#include "trace.h"
#include "process.h"
#include "cpu.h"
#include "heap.h"

#define END_STEP 30

/* -----------------------------------------------------------------------
 * workload_item — raw data read from input file.
 * After the time loop we copy final tf/idle back here for chronogram output.
 * ----------------------------------------------------------------------- */
struct workload_item_t {
    int    pid;
    int    ppid;
    size_t ts;
    size_t tf;
    size_t idle;
    char  *cmd;
    int    prio;
};

enum  epoch { before, in, after };
typedef enum epoch epoch;

/* -----------------------------------------------------------------------
 * Chronogram / display helpers  (unchanged from original)
 * ----------------------------------------------------------------------- */
void draw_hbar(char c, size_t width) {
    char bar[width + 1];
    memset(bar, c, width);
    bar[width] = '\0';
    printf("%s", bar);
}

void chronogram(workload_item *workload, size_t nb_processes, size_t timesteps) {
    size_t tick;
    size_t freq = 5;
    printf("\t");
    for (tick = 0; tick < timesteps; tick++) {
        if (tick % freq == 0) printf("|"); else printf(".");
    }
    printf("\n");
    for (size_t i = 0; i < nb_processes; i++) {
        printf("%s\t", workload[i].cmd);
        draw_hbar(' ', workload[i].ts);
        draw_hbar('X', workload[i].tf - workload[i].ts);
        printf("\t\t\t (tf=%zu,idle=%zu)\n", workload[i].tf, workload[i].idle);
    }
}

/* -----------------------------------------------------------------------
 * File I/O helpers  (unchanged from original)
 * ----------------------------------------------------------------------- */
size_t count_lines_in_file(FILE *file) {
    int    lines = 0;
    char   ch;
    while ((ch = fgetc(file)) != EOF) {
        if (ch == '\n') lines++;
    }
    if (ch != '\n' && lines != 0) lines++;
    rewind(file);
    return lines;
}

size_t read_data(size_t workload_size, FILE *file) {
    size_t count = 0;
    char   line[256];
    char   cmd[50];

    while (fgets(line, sizeof(line), file) && count < workload_size) {
        workload_item item;
        if (sscanf(line, "%d %d %zu %zu %zu %s %d",
                   &item.pid, &item.ppid, &item.ts, &item.tf,
                   &item.idle, cmd, &item.prio) == 7) {
            item.cmd       = strdup(cmd);
            workload[count++] = item;
        } else {
            fprintf(stderr, "Error parsing line: %s\n", line);
            return false;
        }
    }
    return count;
}

/* -----------------------------------------------------------------------
 * remove_finished_processes
 *
 * Scan every slot in runningQueue and evict entries whose tf < t.
 * Updates *prioritySum accordingly.
 * Uses minheap_remove_at so the heap invariant is maintained.
 * ----------------------------------------------------------------------- */
static void remove_finished_processes(MinHeap *runningQueue, size_t t,
                                      int *prioritySum) {
    size_t i = 1;
    while (i <= runningQueue->size) {
        process_t *proc = runningQueue->array[i];
        if (proc->tf < t) {
            printf("  > process pid=%d prio=%d ('%s') finished after t=%zu\n",
                   proc->pid, proc->priority, proc->cmd, t - 1);
            *prioritySum -= proc->priority;
            minheap_remove_at(runningQueue, i);
            /* do NOT increment i — the slot was filled by the last element */
        } else {
            i++;
        }
    }
}

/* -----------------------------------------------------------------------
 * Monotonically increasing sequence counter.
 * Stamped onto process_t just before each maxheap_insert so the MaxHeap
 * can use FIFO ordering as a final tiebreaker within equal priority+idle.
 * ----------------------------------------------------------------------- */
static size_t g_sched_seq = 0;

/* -----------------------------------------------------------------------
 * add_arrived_to_pending
 *
 * Any process whose ts == t is newly available — insert into pendingQueue.
 * ----------------------------------------------------------------------- */
static void add_arrived_to_pending(MaxHeap *pendingQueue,
                                   process_t *procs, size_t procCount,
                                   size_t t) {
    for (size_t i = 0; i < procCount; i++) {
        if (procs[i].ts == t) {
            procs[i].seq = g_sched_seq++;
            maxheap_insert(pendingQueue, &procs[i]);
            printf("  > pid=%d prio=%d ('%s') queued into pending\n",
                   procs[i].pid, procs[i].priority, procs[i].cmd);
        }
    }
}

/* -----------------------------------------------------------------------
 * increment_idle_counters
 *
 * For every process still in pendingQueue after the scheduling pass,
 * increment its idle counter by 1.  tf was already extended once in
 * step 3 (either the preemption evict path or the cannot-fit path), so
 * we do NOT touch tf here — that would double-count.
 *
 * The oldTf snapshot is no longer needed for this function; it was
 * removed along with the incorrect "first-time vs subsequent" branching
 * that caused idle to reset to 1 instead of accumulating.
 * ----------------------------------------------------------------------- */
static void increment_idle_counters(MaxHeap *pendingQueue) {
    for (size_t i = 1; i <= pendingQueue->size; i++) {
        process_t *proc = pendingQueue->array[i];
        proc->idle++;
        printf("  > idle pid=%d prio=%d ('%s') tf=%zu idle=%zu\n",
               proc->pid, proc->priority, proc->cmd, proc->tf, proc->idle);
    }
}

/* -----------------------------------------------------------------------
 * record_state
 *
 * Walk both heaps and call record_timeline with the current running / pending
 * sets, expressed as process[] arrays (pid + prio pairs) as expected by the
 * existing trace infrastructure.
 * ----------------------------------------------------------------------- */
static void record_state(size_t t, size_t tf_limit,
                         pstate **timeline,
                         MinHeap *runningQueue, MaxHeap *pendingQueue,
                         process_t *procs, size_t procCount,
                         size_t workload_size) {
    process *run  = malloc(sizeof(process) * runningQueue->size);
    process *pend = malloc(sizeof(process) * (pendingQueue->size + procCount));

    size_t nb_run  = 0;
    size_t nb_pend = 0;

    for (size_t i = 1; i <= runningQueue->size; i++) {
        process_t *p = runningQueue->array[i];
        if (p->tf >= t) {
            run[nb_run].pid  = p->pid;
            run[nb_run].prio = p->priority;
            nb_run++;
        }
    }

    for (size_t i = 1; i <= pendingQueue->size; i++) {
        process_t *p = pendingQueue->array[i];
        if (p->tf >= t) {
            pend[nb_pend].pid  = p->pid;
            pend[nb_pend].prio = p->priority;
            nb_pend++;
        }
    }

    /* Also mark processes that haven't arrived yet as pending */
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

/* -----------------------------------------------------------------------
 * time_loop  — main scheduling loop
 *
 * Builds a process_t pool from workload[], then runs the two-queue
 * priority scheduler for every tick in [ts, tf].
 * At the end, writes final tf/idle back to workload[] for chronogram output.
 * ----------------------------------------------------------------------- */
void time_loop(size_t workload_size, size_t ts, size_t tf,
               size_t ncpus, pstate **timeline) {
    (void)ncpus; /* single logical CPU with capacity CPU_CAPABILITY */

    /* ------------------------------------------------------------------
     * Allocate one process_t per workload entry.
     * These are the live scheduling objects — heaps point into this pool.
     * ------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------
     * Two scheduling queues.
     * Capacity = workload_size is safe: each process is in at most one
     * queue at a time.
     * ------------------------------------------------------------------ */
    MinHeap runningQueue;
    MaxHeap pendingQueue;
    minheap_init(&runningQueue, workload_size);
    maxheap_init(&pendingQueue, workload_size);

    int prioritySum = 0; /* sum of priorities of currently running processes */

    /* ------------------------------------------------------------------
     * Main scheduling loop
     * ------------------------------------------------------------------ */
    for (size_t t = ts; t <= tf; t++) {
        printf("[t=%zu]\n", t);

        /* Step 1: evict finished processes from runningQueue */
        remove_finished_processes(&runningQueue, t, &prioritySum);

        /* Step 2: enqueue newly arrived processes into pendingQueue */
        add_arrived_to_pending(&pendingQueue, procs, workload_size, t);

        /* ------------------------------------------------------------------
         * Step 3: promote pending -> running (with preemption).
         *
         * We pop the best candidate from pendingQueue and try to fit it.
         * If it fits: insert into runningQueue.
         * If it doesn't fit: compare against the weakest running process.
         *   - candidate.priority > lowest.priority  => preempt lowest.
         *   - candidate.priority <= lowest.priority => no preemption;
         *       bump candidate's tf and hold it.
         * Held-back processes are collected in holdQueue and re-inserted
         * into pendingQueue after the scheduling pass (matching the
         * reference's pattern exactly).
         * ------------------------------------------------------------------ */
        process_t **holdQueue = malloc(sizeof(process_t *) * pendingQueue.capacity + 1);
        size_t holdCount = 0;

        while (!maxheap_empty(&pendingQueue)) {
            process_t *candidate = maxheap_top(&pendingQueue);

            if (prioritySum + candidate->priority <= CPU_CAPABILITY) {
                /* Candidate fits — schedule it */
                maxheap_pop(&pendingQueue);
                printf("  > scheduled pid=%d prio=%d ('%s') pending -> running\n",
                       candidate->pid, candidate->priority, candidate->cmd);
                minheap_insert(&runningQueue, candidate);
                prioritySum += candidate->priority;
                printf("  > CPU load: %d / %d\n", prioritySum, CPU_CAPABILITY);

            } else {
                /* Candidate doesn't fit — check for preemption */
                process_t *lowest = minheap_top(&runningQueue);

                if (lowest != NULL && candidate->priority > lowest->priority) {
                    /* Preempt the lowest-priority running process */
                    maxheap_pop(&pendingQueue);
                    printf("  > scheduled pid=%d prio=%d ('%s') pending -> running (preempting)\n",
                           candidate->pid, candidate->priority, candidate->cmd);
                    minheap_insert(&runningQueue, candidate);
                    prioritySum += candidate->priority;

                    /* Evict until we are back within capacity */
                    while (prioritySum > CPU_CAPABILITY && !minheap_empty(&runningQueue)) {
                        process_t *evict = minheap_top(&runningQueue);
                        minheap_pop(&runningQueue);
                        prioritySum -= evict->priority;

                        printf("  > preempted pid=%d prio=%d ('%s') running -> pending\n",
                               evict->pid, evict->priority, evict->cmd);

                        /* Always extend tf: this process lost a runnable tick */
                        evict->tf++;
                        holdQueue[holdCount++] = evict;

                        printf("  > CPU load: %d / %d\n", prioritySum, CPU_CAPABILITY);
                    }

                } else {
                    /* No preemption possible — candidate stays pending */
                    printf("  > pid=%d prio=%d ('%s') cannot fit, no preemption\n",
                           candidate->pid, candidate->priority, candidate->cmd);
                    maxheap_pop(&pendingQueue);
                    candidate->tf++;          /* lost a runnable tick */
                    holdQueue[holdCount++] = candidate;
                }
            }
        }

        /* Re-insert held-back processes into pendingQueue */
        for (size_t i = 0; i < holdCount; i++) {
            holdQueue[i]->seq = g_sched_seq++;
            maxheap_insert(&pendingQueue, holdQueue[i]);
        }
        free(holdQueue);

        /* Step 4: increment idle counters for processes still pending */
        increment_idle_counters(&pendingQueue);

        /* Step 5: record timeline and print tick summary */
        record_state(t, tf + 1, timeline,
                     &runningQueue, &pendingQueue,
                     procs, workload_size, workload_size);
    }

    /* ------------------------------------------------------------------
     * Write final tf/idle back to workload[] so chronogram() can use them.
     * ------------------------------------------------------------------ */
    for (size_t i = 0; i < workload_size; i++) {
        workload[i].tf   = procs[i].tf;
        workload[i].idle = procs[i].idle;
    }

    minheap_free(&runningQueue);
    maxheap_free(&pendingQueue);
    free(procs);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv) {
    FILE *input;
    if (argc > 1) {
        if ((input = fopen(argv[1], "r")) == NULL) {
            perror("Error reading file:");
            exit(EXIT_FAILURE);
        } else {
            printf("* Read from %s ...", argv[1]);
        }
    } else {
        printf("* Read from stdin ...");
        input = stdin;
    }

    fflush(stdout);
    size_t nr = count_lines_in_file(input);
    printf(" %zu lines in data.\n", nr);

    workload = malloc(sizeof(workload_item) * nr);
    size_t workload_size = read_data(nr, input);
    printf("* Loaded %zu lines of data.\n", nr);

    pstate **timeline = alloc_timeline(END_STEP, workload_size);

    if (nr > 0)
        time_loop(workload_size, 0, END_STEP - 1, MAX_CPU, timeline);
    else
        return EXIT_FAILURE;

    printf("* Chronogram === \n");
    chronogram(workload, workload_size, END_STEP - 1);
    print_timeline(END_STEP - 1, workload_size, timeline);
    free(workload);
    return 0;
}