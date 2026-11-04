/**
 * ============================================================================
 * Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
 * Module: SAT / CDCL / Clauses
 * File: src/SAT/CDCL/Clauses/clause_array.c
 * Description: Implementation of contiguous clause arena storage, watcher lists,
 *              and in-place sliding compaction.
 * ============================================================================
 */

#include "clause_array.h"
#include <stdlib.h>
#include <stdio.h>

#define DEFAULT_INITIAL_WORDS 65536U /* 64K words = 256 KB */
#define DEFAULT_WATCHER_CAPACITY 4U

/* ========================================================================= */
/* 1. Lifecycle & Arena Allocation                                           */
/* ========================================================================= */

bool clause_array_init(ClauseArray* ca, size_t initial_words) {
    if (!ca) return false;

    if (initial_words == 0) {
        initial_words = DEFAULT_INITIAL_WORDS;
    }

    /* Allocate contiguous 32-bit word buffer */
    ca->memory = (uint32_t*)malloc(initial_words * sizeof(uint32_t));
    if (!ca->memory) {
        return false;
    }

    ca->capacity     = initial_words;
    /*
     * Offset 0 is permanently reserved as CLAUSE_REF_NULL (sentinel).
     * First valid clause begins at offset 1.
     */
    ca->memory[0]    = 0U;
    ca->used_words   = 1U;
    ca->wasted_words = 0U;
    ca->clause_count = 0U;
    ca->learned_count = 0U;

    return true;
}

void clause_array_destroy(ClauseArray* ca) {
    if (!ca) return;

    if (ca->memory) {
        free(ca->memory);
        ca->memory = NULL;
    }
    ca->capacity      = 0U;
    ca->used_words    = 0U;
    ca->wasted_words  = 0U;
    ca->clause_count  = 0U;
    ca->learned_count = 0U;
}

void clause_array_clear(ClauseArray* ca) {
    if (!ca) return;

    /* Reset to empty state while retaining allocated buffer capacity */
    if (ca->memory) {
        ca->memory[0] = 0U;
    }
    ca->used_words    = 1U;
    ca->wasted_words  = 0U;
    ca->clause_count  = 0U;
    ca->learned_count = 0U;
}

/* ========================================================================= */
/* 2. Clause Allocation & Tombstoning                                        */
/* ========================================================================= */

ClauseRef clause_array_allocate(ClauseArray* ca,
                                const int32_t* literals,
                                int size,
                                bool is_learned,
                                float activity) {
    if (!ca || !ca->memory || size <= 0 || !literals) {
        return CLAUSE_REF_NULL;
    }

    size_t required_words = (size_t)CLAUSE_HEADER_WORDS + (size_t)size;

    /* Check capacity and grow exponentially if needed */
    if (ca->used_words + required_words > ca->capacity) {
        size_t new_capacity = ca->capacity ? ca->capacity * 2U : DEFAULT_INITIAL_WORDS;
        while (ca->used_words + required_words > new_capacity) {
            new_capacity *= 2U;
        }

        uint32_t* new_mem = (uint32_t*)realloc(ca->memory, new_capacity * sizeof(uint32_t));
        if (!new_mem) {
            return CLAUSE_REF_NULL;
        }

        ca->memory   = new_mem;
        ca->capacity = new_capacity;
    }

    ClauseRef ref = (ClauseRef)ca->used_words;

    /* Word 0: Packed header (size | learned_flag) */
    uint32_t header = ((uint32_t)size & CLAUSE_SIZE_MASK);
    if (is_learned) {
        header |= CLAUSE_LEARNED_FLAG;
    }
    ca->memory[ref] = header;

    /* Word 1: 32-bit float activity score */
    memcpy(&ca->memory[ref + 1], &activity, sizeof(float));

    /* Words 2 .. 1+size: Literals */
    memcpy(&ca->memory[ref + CLAUSE_HEADER_WORDS], literals, (size_t)size * sizeof(int32_t));

    ca->used_words += required_words;
    ca->clause_count++;
    if (is_learned) {
        ca->learned_count++;
    }

    return ref;
}

void clause_array_mark_deleted(ClauseArray* ca, ClauseRef ref) {
    if (!ca || !ca->memory || ref == CLAUSE_REF_NULL || ref >= ca->used_words) {
        return;
    }

    uint32_t header = ca->memory[ref];
    if (header & CLAUSE_DELETED_FLAG) {
        return; /* Already tombstoned */
    }

    ca->memory[ref] |= CLAUSE_DELETED_FLAG;

    int size = (int)(header & CLAUSE_SIZE_MASK);
    size_t words = (size_t)CLAUSE_HEADER_WORDS + (size_t)size;
    ca->wasted_words += words;

    if (header & CLAUSE_LEARNED_FLAG) {
        if (ca->learned_count > 0) {
            ca->learned_count--;
        }
    }
}

/* ========================================================================= */
/* 3. Two-Watched-Literal (2-WL) Helper Implementations                      */
/* ========================================================================= */

bool watch_list_init(WatchList* wl, uint32_t initial_capacity) {
    if (!wl) return false;

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_WATCHER_CAPACITY;
    }

    wl->watchers = (Watcher*)malloc(initial_capacity * sizeof(Watcher));
    if (!wl->watchers) {
        return false;
    }

    wl->size     = 0U;
    wl->capacity = initial_capacity;
    return true;
}

bool watch_list_push(WatchList* wl, ClauseRef ref, int32_t blocking_literal) {
    if (!wl) return false;

    if (wl->size >= wl->capacity) {
        uint32_t new_cap = wl->capacity ? wl->capacity * 2U : DEFAULT_WATCHER_CAPACITY;
        Watcher* new_watchers = (Watcher*)realloc(wl->watchers, new_cap * sizeof(Watcher));
        if (!new_watchers) {
            return false;
        }
        wl->watchers = new_watchers;
        wl->capacity = new_cap;
    }

    wl->watchers[wl->size].clause_ref        = ref;
    wl->watchers[wl->size].blocking_literal  = blocking_literal;
    wl->size++;
    return true;
}

void watch_list_destroy(WatchList* wl) {
    if (!wl) return;

    if (wl->watchers) {
        free(wl->watchers);
        wl->watchers = NULL;
    }
    wl->size     = 0U;
    wl->capacity = 0U;
}

/* ========================================================================= */
/* 4. In-Place Sliding Compaction & Database Reduction                       */
/* ========================================================================= */

bool clause_array_compaction_needed(const ClauseArray* ca, double wasted_threshold) {
    if (!ca || ca->used_words <= 1U || wasted_threshold <= 0.0) {
        return false;
    }
    double ratio = (double)ca->wasted_words / (double)ca->used_words;
    return ratio >= wasted_threshold;
}

void clause_array_compact(ClauseArray* ca, ClauseRemapCallback remap_cb, void* ctx) {
    if (!ca || !ca->memory || ca->wasted_words == 0) {
        return;
    }

    size_t read_cursor  = 1U; /* Start after CLAUSE_REF_NULL */
    size_t write_cursor = 1U;
    size_t active_clauses = 0;
    size_t active_learned = 0;

    while (read_cursor < ca->used_words) {
        uint32_t header = ca->memory[read_cursor];
        int size = (int)(header & CLAUSE_SIZE_MASK);
        size_t clause_words = (size_t)CLAUSE_HEADER_WORDS + (size_t)size;

        if (header & CLAUSE_DELETED_FLAG) {
            /* Tombstoned clause: skip it */
            read_cursor += clause_words;
            continue;
        }

        /* Active clause: slide it down if cursors have diverged */
        if (write_cursor != read_cursor) {
            memmove(&ca->memory[write_cursor],
                    &ca->memory[read_cursor],
                    clause_words * sizeof(uint32_t));
        }

        /* Inform external solver of the relocated clause reference */
        if (remap_cb) {
            remap_cb((ClauseRef)read_cursor, (ClauseRef)write_cursor, ctx);
        }

        active_clauses++;
        if (header & CLAUSE_LEARNED_FLAG) {
            active_learned++;
        }

        write_cursor += clause_words;
        read_cursor  += clause_words;
    }

    ca->used_words    = write_cursor;
    ca->wasted_words  = 0U;
    ca->clause_count  = active_clauses;
    ca->learned_count = active_learned;
}
