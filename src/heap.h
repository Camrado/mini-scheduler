/**
 * @file heap.h
 * @brief Shared definitions for MinHeap and MaxHeap used by the scheduler.
 *
 * process_t is the element type stored in both heaps.
 * The heaps store process_t* (pointers) in a 1-based array.
 *
 * Two-queue scheduling model (adapted from reference):
 *
 *   runningQueue (MinHeap, key = priority):
 *     Holds currently executing processes.
 *     MinHeap so the *lowest-priority* running process bubbles to the top,
 *     enabling O(log n) preemption checks when a higher-priority process
 *     arrives but the CPU is already at capacity.
 *
 *   pendingQueue (MaxHeap, key = priority):
 *     Holds ready-but-not-yet-running processes (arrived, not finished,
 *     not currently scheduled).
 *     MaxHeap so the highest-priority candidate is always tried first.
 *
 * process_t carries all per-process mutable state so the heaps own the
 * data directly, avoiding stale-pointer hazards from a separate workload[].
 */

#ifndef _HEAP_H_
#define _HEAP_H_

#include <stdlib.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Element type stored in both heaps.
 * Carries all mutable scheduling state for one process.
 * ----------------------------------------------------------------------- */
typedef struct {
    int    pid;         /**< unique process id (== workload[] index)        */
    int    ppid;        /**< parent pid                                      */
    size_t ts;          /**< arrival / start time (immutable after load)     */
    size_t tf;          /**< current finish time   (grows when process idles)*/
    size_t idle;        /**< accumulated idle ticks                          */
    char   cmd[32];     /**< command name (for logging / chronogram)         */
    int    priority;    /**< cpu scheduling priority — heap key in both queues*/
    size_t seq;   /* insertion sequence number — FIFO tiebreak in MaxHeap */

} process_t;

/* -----------------------------------------------------------------------
 * 1-based binary heap index macros
 * ----------------------------------------------------------------------- */
#define HEAP_PARENT(i)  ((i) / 2)
#define HEAP_LEFT(i)    ((i) * 2)
#define HEAP_RIGHT(i)   ((i) * 2 + 1)

/* -----------------------------------------------------------------------
 * MinHeap — root is the element with the *lowest* priority value.
 * Used as runningQueue: the weakest running process is always at top,
 * ready to be preempted.
 * ----------------------------------------------------------------------- */
typedef struct {
    process_t **array;   /**< 1-based array of pointers */
    size_t      size;
    size_t      capacity;
} MinHeap;

void       minheap_init     (MinHeap *h, size_t capacity);
void       minheap_insert   (MinHeap *h, process_t *proc);
process_t *minheap_top      (MinHeap *h);
void       minheap_pop      (MinHeap *h);
bool       minheap_empty    (MinHeap *h);
void       minheap_free     (MinHeap *h);
void       minheap_remove_at(MinHeap *h, size_t idx);
void       min_heapify_down (MinHeap *h, size_t idx);
void       min_heapify_up   (MinHeap *h, size_t idx);

/* -----------------------------------------------------------------------
 * MaxHeap — root is the element with the *highest* priority value.
 * Used as pendingQueue: the best candidate is always tried first.
 * ----------------------------------------------------------------------- */
typedef struct {
    process_t **array;   /**< 1-based array of pointers */
    size_t      size;
    size_t      capacity;
} MaxHeap;

void       maxheap_init     (MaxHeap *h, size_t capacity);
void       maxheap_insert   (MaxHeap *h, process_t *proc);
process_t *maxheap_top      (MaxHeap *h);
void       maxheap_pop      (MaxHeap *h);
bool       maxheap_empty    (MaxHeap *h);
void       maxheap_free     (MaxHeap *h);
void       maxheap_remove_at(MaxHeap *h, size_t idx);
void       max_heapify_down (MaxHeap *h, size_t idx);
void       max_heapify_up   (MaxHeap *h, size_t idx);

#endif /* _HEAP_H_ */