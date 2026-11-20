#ifndef PRIORITY_QUEUE
#define PRIORITY_QUEUE

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define INVALID_POSITION SIZE_MAX

typedef unsigned int ElementID;

/**
 * Comparator function type for ordering elements in the priority queue.
 *
 * @param first  First element ID to compare.
 * @param second Second element ID to compare.
 * @return Negative if first has lower priority,
 *         Zero if priorities are equal,
 *         Positive if first has higher priority.
 */
typedef int (*PriorityComparator)(ElementID first, ElementID second);

typedef struct PriorityQueue {
    ElementID* heap;
    size_t* position;

    size_t size;
    size_t capacity;

    PriorityComparator compare;
} PriorityQueue;

bool priority_queue_initialize(
    PriorityQueue* queue,
    size_t initial_capacity,
    PriorityComparator comparator
);

bool priority_queue_insert(
    PriorityQueue* queue,
    ElementID element
);

bool priority_queue_peek_max(
    PriorityQueue* queue,
    ElementID* result
);

bool priority_queue_extract_max(
    PriorityQueue* queue,
    ElementID* result
);

bool priority_queue_update(
    PriorityQueue* queue,
    ElementID element
);

bool priority_queue_remove(
    PriorityQueue* queue,
    ElementID element
);

bool priority_queue_contains(
    PriorityQueue* queue,
    ElementID element
);

bool priority_queue_is_empty(
    PriorityQueue* queue
);

bool priority_queue_validate(
    PriorityQueue* queue
);

void priority_queue_destroy(
    PriorityQueue* queue
);

#endif
