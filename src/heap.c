#include <stdio.h>
#include <stdlib.h>
#include "heap.h"

/* ========================= MinHeap ========================= */

void minheap_init(MinHeap *h, size_t capacity) {
    h->array = malloc(sizeof(process_t *) * (capacity + 1)); /* 1-based */
    if (!h->array) {
        perror("minheap_init: malloc");
        exit(EXIT_FAILURE);
    }
    h->size = 0;
    h->capacity = capacity;
}

bool minheap_empty(MinHeap *h) {
    return h == NULL || h->size == 0;
}

static void minheap_swap(process_t **a, process_t **b) {
    process_t *tmp = *a;
    *a = *b;
    *b = tmp;
}

void min_heapify_up(MinHeap *h, size_t idx) {
    while (idx > 1) {
        size_t parent = HEAP_PARENT(idx);
        if (h->array[parent]->priority <= h->array[idx]->priority) {
            break;
        }
        minheap_swap(&h->array[parent], &h->array[idx]);
        idx = parent;
    }
}

void min_heapify_down(MinHeap *h, size_t idx) {
    while (true) {
        size_t left = HEAP_LEFT(idx);
        size_t right = HEAP_RIGHT(idx);
        size_t smallest = idx;

        if (left <= h->size &&
            h->array[left]->priority < h->array[smallest]->priority) {
            smallest = left;
        }

        if (right <= h->size &&
            h->array[right]->priority < h->array[smallest]->priority) {
            smallest = right;
        }

        if (smallest == idx) {
            break;
        }

        minheap_swap(&h->array[idx], &h->array[smallest]);
        idx = smallest;
    }
}

void minheap_insert(MinHeap *h, process_t *proc) {
    if (h->size >= h->capacity) {
        fprintf(stderr, "minheap_insert: heap capacity exceeded\n");
        exit(EXIT_FAILURE);
    }
    h->array[++h->size] = proc;
    min_heapify_up(h, h->size);
}

process_t *minheap_top(MinHeap *h) {
    if (minheap_empty(h)) {
        return NULL;
    }
    return h->array[1];
}

void minheap_pop(MinHeap *h) {
    if (minheap_empty(h)) {
        return;
    }
    h->array[1] = h->array[h->size];
    h->size--;
    if (h->size > 0) {
        min_heapify_down(h, 1);
    }
}

void minheap_remove_at(MinHeap *h, size_t idx) {
    if (h == NULL || idx == 0 || idx > h->size) {
        return;
    }

    if (idx == h->size) {
        h->size--;
        return;
    }

    h->array[idx] = h->array[h->size];
    h->size--;

    if (idx > 1 &&
        h->array[idx]->priority < h->array[HEAP_PARENT(idx)]->priority) {
        min_heapify_up(h, idx);
    } else {
        min_heapify_down(h, idx);
    }
}

void minheap_free(MinHeap *h) {
    if (h == NULL) {
        return;
    }
    free(h->array);
    h->array = NULL;
    h->size = 0;
    h->capacity = 0;
}

/* ========================= MaxHeap ========================= */

void maxheap_init(MaxHeap *h, size_t capacity) {
    h->array = malloc(sizeof(process_t *) * (capacity + 1)); /* 1-based */
    if (!h->array) {
        perror("maxheap_init: malloc");
        exit(EXIT_FAILURE);
    }
    h->size = 0;
    h->capacity = capacity;
}

bool maxheap_empty(MaxHeap *h) {
    return h == NULL || h->size == 0;
}

static void maxheap_swap(process_t **a, process_t **b) {
    process_t *tmp = *a;
    *a = *b;
    *b = tmp;
}

void max_heapify_up(MaxHeap *h, size_t idx) {
    while (idx > 1) {
        size_t parent = HEAP_PARENT(idx);
        if (h->array[parent]->priority >= h->array[idx]->priority) {
            break;
        }
        maxheap_swap(&h->array[parent], &h->array[idx]);
        idx = parent;
    }
}

void max_heapify_down(MaxHeap *h, size_t idx) {
    while (true) {
        size_t left = HEAP_LEFT(idx);
        size_t right = HEAP_RIGHT(idx);
        size_t largest = idx;

        if (left <= h->size &&
            h->array[left]->priority > h->array[largest]->priority) {
            largest = left;
        }

        if (right <= h->size &&
            h->array[right]->priority > h->array[largest]->priority) {
            largest = right;
        }

        if (largest == idx) {
            break;
        }

        maxheap_swap(&h->array[idx], &h->array[largest]);
        idx = largest;
    }
}

void maxheap_insert(MaxHeap *h, process_t *proc) {
    if (h->size >= h->capacity) {
        fprintf(stderr, "maxheap_insert: heap capacity exceeded\n");
        exit(EXIT_FAILURE);
    }
    h->array[++h->size] = proc;
    max_heapify_up(h, h->size);
}

process_t *maxheap_top(MaxHeap *h) {
    if (maxheap_empty(h)) {
        return NULL;
    }
    return h->array[1];
}

void maxheap_pop(MaxHeap *h) {
    if (maxheap_empty(h)) {
        return;
    }
    h->array[1] = h->array[h->size];
    h->size--;
    if (h->size > 0) {
        max_heapify_down(h, 1);
    }
}

void maxheap_remove_at(MaxHeap *h, size_t idx) {
    if (h == NULL || idx == 0 || idx > h->size) {
        return;
    }

    if (idx == h->size) {
        h->size--;
        return;
    }

    h->array[idx] = h->array[h->size];
    h->size--;

    if (idx > 1 &&
        h->array[idx]->priority > h->array[HEAP_PARENT(idx)]->priority) {
        max_heapify_up(h, idx);
    } else {
        max_heapify_down(h, idx);
    }
}

void maxheap_free(MaxHeap *h) {
    if (h == NULL) {
        return;
    }
    free(h->array);
    h->array = NULL;
    h->size = 0;
    h->capacity = 0;
}