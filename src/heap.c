/**
 * @file heap.c
 * @brief MinHeap and MaxHeap implementations for the scheduler.
 *
 * Both heaps store process_t* in a 1-based array.
 * Tie-breaking on equal priority uses pid (lower pid wins for MinHeap,
 * higher pid wins for MaxHeap) — this keeps scheduling deterministic.
 *
 * This file is the single source of truth for heap operations.
 * Do NOT link min_heap.c or max_heap.c alongside this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "heap.h"

/* ========================= Internal helpers ========================= */

static void swap_ptrs(process_t **a, process_t **b) {
    process_t *tmp = *a;
    *a = *b;
    *b = tmp;
}

/**
 * Returns true if a should be *above* b in the MinHeap
 * (i.e. a has a strictly lower priority, or equal priority with lower pid).
 */
static bool min_has_priority(const process_t *a, const process_t *b) {
    if (a->priority != b->priority)
        return a->priority < b->priority;
    return a->pid < b->pid;
}

/**
 * Returns true if a should be *above* b in the MaxHeap
 * (i.e. a has a strictly higher priority, or equal priority with higher pid).
 */
static bool max_has_priority(const process_t *a, const process_t *b) {
    if (a->priority != b->priority)
        return a->priority > b->priority;
    return a->pid > b->pid;
}

/* ========================= MinHeap ========================= */

void minheap_init(MinHeap *h, size_t capacity) {
    /* +1 because we use 1-based indexing (index 0 is unused) */
    h->array = malloc(sizeof(process_t *) * (capacity + 1));
    if (!h->array) {
        perror("minheap_init: malloc");
        exit(EXIT_FAILURE);
    }
    h->size     = 0;
    h->capacity = capacity;
}

bool minheap_empty(MinHeap *h) {
    return h == NULL || h->size == 0;
}

void min_heapify_up(MinHeap *h, size_t idx) {
    while (idx > 1) {
        size_t parent = HEAP_PARENT(idx);
        if (!min_has_priority(h->array[idx], h->array[parent]))
            break;
        swap_ptrs(&h->array[parent], &h->array[idx]);
        idx = parent;
    }
}

void min_heapify_down(MinHeap *h, size_t idx) {
    while (true) {
        size_t left    = HEAP_LEFT(idx);
        size_t right   = HEAP_RIGHT(idx);
        size_t smallest = idx;

        if (left <= h->size &&
            min_has_priority(h->array[left], h->array[smallest]))
            smallest = left;

        if (right <= h->size &&
            min_has_priority(h->array[right], h->array[smallest]))
            smallest = right;

        if (smallest == idx)
            break;

        swap_ptrs(&h->array[idx], &h->array[smallest]);
        idx = smallest;
    }
}

void minheap_insert(MinHeap *h, process_t *proc) {
    if (h->size >= h->capacity) {
        fprintf(stderr, "minheap_insert: heap capacity exceeded (cap=%zu)\n",
                h->capacity);
        exit(EXIT_FAILURE);
    }
    h->array[++h->size] = proc;
    min_heapify_up(h, h->size);
}

process_t *minheap_top(MinHeap *h) {
    return minheap_empty(h) ? NULL : h->array[1];
}

void minheap_pop(MinHeap *h) {
    if (minheap_empty(h))
        return;
    h->array[1] = h->array[h->size];
    h->size--;
    if (h->size > 0)
        min_heapify_down(h, 1);
}

void minheap_remove_at(MinHeap *h, size_t idx) {
    if (h == NULL || idx == 0 || idx > h->size)
        return;
    if (idx == h->size) {
        h->size--;
        return;
    }
    h->array[idx] = h->array[h->size];
    h->size--;
    /* The replacement node might need to go up or down */
    if (idx > 1 && min_has_priority(h->array[idx], h->array[HEAP_PARENT(idx)]))
        min_heapify_up(h, idx);
    else
        min_heapify_down(h, idx);
}

void minheap_free(MinHeap *h) {
    if (h == NULL)
        return;
    free(h->array);
    h->array    = NULL;
    h->size     = 0;
    h->capacity = 0;
}

/* ========================= MaxHeap ========================= */

void maxheap_init(MaxHeap *h, size_t capacity) {
    h->array = malloc(sizeof(process_t *) * (capacity + 1));
    if (!h->array) {
        perror("maxheap_init: malloc");
        exit(EXIT_FAILURE);
    }
    h->size     = 0;
    h->capacity = capacity;
}

bool maxheap_empty(MaxHeap *h) {
    return h == NULL || h->size == 0;
}

void max_heapify_up(MaxHeap *h, size_t idx) {
    while (idx > 1) {
        size_t parent = HEAP_PARENT(idx);
        if (!max_has_priority(h->array[idx], h->array[parent]))
            break;
        swap_ptrs(&h->array[parent], &h->array[idx]);
        idx = parent;
    }
}

void max_heapify_down(MaxHeap *h, size_t idx) {
    while (true) {
        size_t left    = HEAP_LEFT(idx);
        size_t right   = HEAP_RIGHT(idx);
        size_t largest = idx;

        if (left <= h->size &&
            max_has_priority(h->array[left], h->array[largest]))
            largest = left;

        if (right <= h->size &&
            max_has_priority(h->array[right], h->array[largest]))
            largest = right;

        if (largest == idx)
            break;

        swap_ptrs(&h->array[idx], &h->array[largest]);
        idx = largest;
    }
}

void maxheap_insert(MaxHeap *h, process_t *proc) {
    if (h->size >= h->capacity) {
        fprintf(stderr, "maxheap_insert: heap capacity exceeded (cap=%zu)\n",
                h->capacity);
        exit(EXIT_FAILURE);
    }
    h->array[++h->size] = proc;
    max_heapify_up(h, h->size);
}

process_t *maxheap_top(MaxHeap *h) {
    return maxheap_empty(h) ? NULL : h->array[1];
}

void maxheap_pop(MaxHeap *h) {
    if (maxheap_empty(h))
        return;
    h->array[1] = h->array[h->size];
    h->size--;
    if (h->size > 0)
        max_heapify_down(h, 1);
}

void maxheap_remove_at(MaxHeap *h, size_t idx) {
    if (h == NULL || idx == 0 || idx > h->size)
        return;
    if (idx == h->size) {
        h->size--;
        return;
    }
    h->array[idx] = h->array[h->size];
    h->size--;
    if (idx > 1 && max_has_priority(h->array[idx], h->array[HEAP_PARENT(idx)]))
        max_heapify_up(h, idx);
    else
        max_heapify_down(h, idx);
}

void maxheap_free(MaxHeap *h) {
    if (h == NULL)
        return;
    free(h->array);
    h->array    = NULL;
    h->size     = 0;
    h->capacity = 0;
}