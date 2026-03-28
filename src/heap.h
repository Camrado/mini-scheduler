/*
 * heap.h
 *
 * Generic heap used for both queues in the scheduler
 * One struct, one comparator pointer - works for min and max
 *
 * runningQueue  -> min_cmp  (weakest proc at top, easy to preempt)
 * pendingQueue  -> max_cmp  (best candidate always at top)
 *
 * 0-based indexing throughout (parent = (i-1)/2, left = 2i+1, right = 2i+2)
 */

#ifndef HEAP_H
#define HEAP_H

#include <stdlib.h>
#include <stdbool.h>

/* all the info needed per process during scheduling */
typedef struct {
    int    pid;       /* process id */
    int    ppid;      /* parent pid */
    size_t ts;        /* start time, doesn't change after loading */
    size_t tf;        /* finish time - grows if process keeps getting delayed */
    size_t idle;      /* how many ticks this proc has been waiting */
    char   cmd[32];   /* command name */
    int    priority;  /* the key we sort on */
    size_t seq;       /* arrival order, used to break ties fairly */
} process_t;

/* index macros - 0-based */
#define HEAP_PARENT(i)  (((i) - 1) / 2)
#define HEAP_LEFT(i)    ((i) * 2 + 1)
#define HEAP_RIGHT(i)   ((i) * 2 + 2)

/* cmp(a, b) returns true if a should be above b */
typedef bool (*heap_cmp)(const process_t *a, const process_t *b);

/* the heap itself - works for both min and max depending on cmp */
typedef struct {
    process_t **arr;      /* array of pointers, 0-based */
    size_t      size;     /* current number of elements */
    size_t      capacity; /* allocated slots */
    heap_cmp    cmp;      /* comparator set at init */
} Heap;

/*
 * Two comparators:
 * min_cmp: lower priority wins -> used for runningQueue
 * max_cmp: higher priority wins -> used for pendingQueue
 */

/* lower prio value = goes to top. tie break on pid */
bool min_cmp(const process_t *a, const process_t *b);

/* higher prio value = goes to top. tie break on seq (FIFO) */
bool max_cmp(const process_t *a, const process_t *b);

/* --- heap functions --- */

/* set up the heap with given capacity and comparator */
void heap_init(Heap *h, size_t capacity, heap_cmp cmp);

/* returns true if empty */
bool heap_empty(Heap *h);

/* number of elements in heap */
size_t heap_size(Heap *h);

/* peek at top without removing, NULL if empty */
process_t *heap_top(Heap *h);

/* insert a process, grows array if needed */
void heap_insert(Heap *h, process_t *proc);

/* remove and return top element, NULL if empty */
process_t *heap_pop(Heap *h);

/* remove element at index idx (used when scanning for finished procs) */
void heap_remove_at(Heap *h, size_t idx);

/* bubble element up (used after insert) */
void heapify_up(Heap *h, size_t idx);

/* push element down (used after pop or remove) */
void heapify_down(Heap *h, size_t idx);

/* free the array - does NOT free the process_t objects themselves */
void heap_free(Heap *h);

#endif /* HEAP_H */