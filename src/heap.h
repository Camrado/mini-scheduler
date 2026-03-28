/**
 * @file heap.h
 * @brief Shared definitions for MinHeap and MaxHeap used by the scheduler.
 *
 * process_t is the element type stored by both heaps.
 * The heaps store process_t* (pointers) in a 1-based array.
 *
 * Field semantics by use-case:
 *   arrival_heap (MinHeap):  pid = workload index,  priority = ts  (start time)
 *   ready_heap   (MaxHeap):  pid = workload index,  priority = prio (cpu priority)
 *
 * This lets both heaps reuse the same process_t type while ordering
 * on different fields depending on context.
 */

#ifndef _HEAP_H_
#define _HEAP_H_

#include <stdlib.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Element type stored in both heaps
 * ----------------------------------------------------------------------- */
typedef struct {
    int pid;       /**< workload[] index — stable across both heaps */
    int priority;  /**< meaning depends on heap: ts for arrival, prio for ready */
} process_t;

/* -----------------------------------------------------------------------
 * 1-based binary heap index macros
 * ----------------------------------------------------------------------- */
#define HEAP_PARENT(i)  ((i) / 2)
#define HEAP_LEFT(i)    ((i) * 2)
#define HEAP_RIGHT(i)   ((i) * 2 + 1)

/* -----------------------------------------------------------------------
 * MinHeap — root is the element with the *lowest* priority value.
 * Used as arrival_heap (priority = ts: smallest ts rises to top).
 * ----------------------------------------------------------------------- */
typedef struct {
    process_t **array;   /**< 1-based array of pointers */
    size_t      size;
    size_t      capacity;
} MinHeap;

void      minheap_init     (MinHeap *h, size_t capacity);
void      minheap_insert   (MinHeap *h, process_t *proc);
process_t *minheap_top     (MinHeap *h);
void      minheap_pop      (MinHeap *h);
bool      minheap_empty    (MinHeap *h);
void      minheap_free     (MinHeap *h);
void      minheap_remove_at(MinHeap *h, size_t idx);
void      min_heapify_down (MinHeap *h, size_t idx);
void      min_heapify_up   (MinHeap *h, size_t idx);

/* -----------------------------------------------------------------------
 * MaxHeap — root is the element with the *highest* priority value.
 * Used as ready_heap (priority = prio: highest cpu-priority rises to top).
 * ----------------------------------------------------------------------- */
typedef struct {
    process_t **array;   /**< 1-based array of pointers */
    size_t      size;
    size_t      capacity;
} MaxHeap;

void      maxheap_init     (MaxHeap *h, size_t capacity);
void      maxheap_insert   (MaxHeap *h, process_t *proc);
process_t *maxheap_top     (MaxHeap *h);
void      maxheap_pop      (MaxHeap *h);
bool      maxheap_empty    (MaxHeap *h);
void      maxheap_free     (MaxHeap *h);
void      maxheap_remove_at(MaxHeap *h, size_t idx);
void      max_heapify_down (MaxHeap *h, size_t idx);
void      max_heapify_up   (MaxHeap *h, size_t idx);

#endif /* _HEAP_H_ */