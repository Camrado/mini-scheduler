/*
 * heap.c
 *
 * Implementation of the generic heap used by the scheduler
 * Same struct handles both min-heap (running queue) and max-heap (pending queue)
 * by passing a different comparator at init time
 *
 * Based on the reference heap from the lab, adapted to store process_t pointers
 * instead of plain ints and to support both orderings
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "heap.h"

/* swap two pointers in the array */
static void swap(process_t **a, process_t **b) {
    process_t *tmp = *a;
    *a = *b;
    *b = tmp;
}

/*
 * Comparators
 */

/* min_cmp: used for runningQueue
 * lower priority value = closer to top (weakest proc is easiest to evict)
 * tie-break on pid so ordering stays deterministic */
bool min_cmp(const process_t *a, const process_t *b) {
    if (a->priority != b->priority)
        return a->priority < b->priority;
    return a->pid > b->pid;
}

/* max_cmp: used for pendingQueue
 * higher priority = closer to top (best proc gets scheduled first)
 * same priority -> lower seq wins (FIFO, earlier arrivals go first) */
bool max_cmp(const process_t *a, const process_t *b) {
    if (a->priority != b->priority)
        return a->priority > b->priority;
    return a->seq < b->seq;
}

/* allocate backing array and set fields */
void heap_init(Heap *h, size_t capacity, heap_cmp cmp) {
    assert(h && cmp);
    h->arr = malloc(sizeof(process_t *) * capacity);
    if (!h->arr) {
        perror("heap_init: malloc");
        exit(EXIT_FAILURE);
    }
    h->size     = 0;
    h->capacity = capacity;
    h->cmp      = cmp;
}

bool heap_empty(Heap *h) {
    return h == NULL || h->size == 0;
}

size_t heap_size(Heap *h) {
    return (h == NULL) ? 0 : h->size;
}

/* top element is always arr[0] in a valid heap */
process_t *heap_top(Heap *h) {
    return heap_empty(h) ? NULL : h->arr[0];
}

/*
 * heapify_down - fix heap order going downward from idx
 * same idea as max_heapify from the reference, but uses our cmp
 */
void heapify_down(Heap *h, size_t idx) {
    while (true) {
        size_t left    = HEAP_LEFT(idx);
        size_t right   = HEAP_RIGHT(idx);
        size_t best    = idx;           /* track which node should be on top */

        /* check left child */
        if (left < h->size && h->cmp(h->arr[left], h->arr[best]))
            best = left;

        /* check right child */
        if (right < h->size && h->cmp(h->arr[right], h->arr[best]))
            best = right;

        if (best == idx)
            break;                     

        swap(&h->arr[idx], &h->arr[best]);
        idx = best;
    }
}

/*
 * heapify_up - fix heap order going upward from idx
 * used after insert, mirrors the increase-key loop from the reference
 */
void heapify_up(Heap *h, size_t idx) {
    while (idx > 0) {
        size_t parent = HEAP_PARENT(idx);
        if (!h->cmp(h->arr[idx], h->arr[parent]))
            break;                      /* already in right place */
        swap(&h->arr[parent], &h->arr[idx]);
        idx = parent;
    }
}

/*
 * heap_insert - add proc at the end then bubble up
 * doubles capacity if we run out of space
 */
void heap_insert(Heap *h, process_t *proc) {
    assert(h && proc);

    /* grow if needed */
    if (h->size >= h->capacity) {
        h->capacity = h->capacity ? h->capacity * 2 : 1;
        h->arr = realloc(h->arr, sizeof(process_t *) * h->capacity);
        if (!h->arr) {
            perror("heap_insert: realloc");
            exit(EXIT_FAILURE);
        }
    }

    h->arr[h->size] = proc;   /* append at end */
    h->size++;
    heapify_up(h, h->size - 1);
}

/*
 * heap_pop - remove and return the top element
 * move last element to root, decrement size, fix downward
 */
process_t *heap_pop(Heap *h) {
    if (heap_empty(h))
        return NULL;

    process_t *top = h->arr[0];          /* save what we're returning */
    h->arr[0] = h->arr[h->size - 1];    /* put last element at root */
    h->size--;
    if (h->size > 0)
        heapify_down(h, 0);

    return top;
}

/*
 * heap_remove_at - remove element at arbitrary index
 * needed for remove_finished_processes which scans the whole array
 * replace the slot with the last element then fix up or down as needed
 */
void heap_remove_at(Heap *h, size_t idx) {
    if (h == NULL || idx >= h->size)
        return;

    /* removing the last element, just shrink */
    if (idx == h->size - 1) {
        h->size--;
        return;
    }

    h->arr[idx] = h->arr[h->size - 1];  /* overwrite with last */
    h->size--;

    /* the replacement might need to go up or down, check both */
    if (idx > 0 && h->cmp(h->arr[idx], h->arr[HEAP_PARENT(idx)]))
        heapify_up(h, idx);
    else
        heapify_down(h, idx);
}

/* free the backing array only - process_t objects live in procs[] in time_loop */
void heap_free(Heap *h) {
    if (h == NULL)
        return;
    free(h->arr);
    h->arr      = NULL;
    h->size     = 0;
    h->capacity = 0;
}