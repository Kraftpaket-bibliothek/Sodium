/**
 * ============================================================================
 * Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
 * Module: SAT / CDCL / Clauses
 * File: src/SAT/CDCL/Clauses/clause_array.h
 * Description: High-performance contiguous memory arena for raw Boolean clauses
 *              and Two-Watched-Literal (2-WL) watcher structures.
 *
 * Architecture:
 *   - Strictly contiguous 32-bit unsigned integer array (`uint32_t`).
 *   - Completely separated from the general-purpose `MemoryManager`.
 *   - Clauses addressed by 32-bit integer offset (`ClauseRef`).
 *   - Packed 32-bit header (28-bit size, 1-bit learned, 1-bit deleted, 2-bit flags).
 *   - Native Two-Watched-Literal support with watcher blocking literals for
 *     near-zero cache misses during Boolean Constraint Propagation (BCP).
 * ============================================================================
 */

#ifndef SODIUM_CLAUSE_ARRAY_H
#define SODIUM_CLAUSE_ARRAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* 1. Constants & Type Definitions                                           */
/* ========================================================================= */

/**
 * @brief Sentinel null reference for a clause in the arena.
 *        Offset 0 is permanently reserved as invalid / NULL.
 */
#define CLAUSE_REF_NULL 0U

/**
 * @brief 32-bit offset index referencing a clause in the ClauseArray arena.
 *        Can index up to 2^32 32-bit words (16 Gigabytes of clauses).
 */
typedef uint32_t ClauseRef;

/**
 * @brief Header bit-mask definitions for Word 0 of a clause.
 *        Bits  0..27: Clause length (up to 268,435,455 literals)
 *        Bit      28: is_learned flag (1 = learned/lemma, 0 = original)
 *        Bit      29: is_deleted flag (1 = tombstoned, 0 = active)
 *        Bits 30..31: Reserved (glue / LBD flags)
 */
#define CLAUSE_SIZE_MASK         0x0FFFFFFFU
#define CLAUSE_LEARNED_FLAG      (1U << 28)
#define CLAUSE_DELETED_FLAG      (1U << 29)
#define CLAUSE_HEADER_WORDS      2U

/**
 * @brief Two-Watched-Literal (2-WL) Watcher entry.
 *        Packed into exactly 8 bytes (two 32-bit words).
 *
 * The blocking_literal is stored inline alongside the clause reference:
 * During BCP, if the blocking literal is already TRUE under the current
 * assignment, the clause is guaranteed satisfied and the solver does NOT
 * read the clause from memory, eliminating over 80% of RAM/cache traffic.
 */
typedef struct Watcher {
    ClauseRef clause_ref;        /**< 32-bit offset into ClauseArray arena */
    int32_t   blocking_literal;  /**< Other watched literal for cache bypass */
} Watcher;

/**
 * @brief Dynamic contiguous array of Watchers for a single signed literal.
 */
typedef struct WatchList {
    Watcher* watchers;
    uint32_t size;
    uint32_t capacity;
} WatchList;

/**
 * @brief Contiguous clause arena buffer.
 *
 * Memory Layout per Clause (K literals):
 *   Word 0:         [ Header: size (28b) | is_learned (1b) | is_deleted (1b) | flags (2b) ]
 *   Word 1:         [ Activity: 32-bit IEEE-754 single-precision float ]
 *   Word 2:         [ Literal 0: 1st Watched Literal (int32_t) ]
 *   Word 3:         [ Literal 1: 2nd Watched Literal (int32_t) ]
 *   Words 4..(1+K): [ Literals 2..K-1 (int32_t) ]
 */
typedef struct ClauseArray {
    uint32_t* memory;            /**< Contiguous 32-bit word storage */
    size_t    capacity;          /**< Total words allocated in memory */
    size_t    used_words;        /**< Words currently occupied */
    size_t    wasted_words;      /**< Tombstoned / deleted words */
    size_t    clause_count;      /**< Total active + tombstoned clauses */
    size_t    learned_count;     /**< Total active learned clauses */
} ClauseArray;

/* ========================================================================= */
/* 2. Lifecycle & Memory Management API                                      */
/* ========================================================================= */

/**
 * @brief Allocates and initializes a new ClauseArray arena.
 *
 * @param ca             Pointer to ClauseArray struct.
 * @param initial_words  Initial capacity in 32-bit words (0 for default 64K words).
 * @return true on success, false on allocation failure.
 */
bool clause_array_init(ClauseArray* ca, size_t initial_words);

/**
 * @brief Releases all allocated arena storage and resets struct counters.
 *
 * @param ca Pointer to ClauseArray struct.
 */
void clause_array_destroy(ClauseArray* ca);

/**
 * @brief Resets the arena for reuse without freeing the underlying buffer.
 *
 * @param ca Pointer to ClauseArray struct.
 */
void clause_array_clear(ClauseArray* ca);

/* ========================================================================= */
/* 3. Clause Allocation & Tombstoning API                                    */
/* ========================================================================= */

/**
 * @brief Allocates a new clause sequentially in the contiguous arena.
 *
 * @param ca          ClauseArray instance.
 * @param literals    Array of signed non-zero literals.
 * @param size        Number of literals in clause (must be >= 1).
 * @param is_learned  true if learned clause, false if original problem clause.
 * @param activity    Initial floating-point activity score.
 * @return ClauseRef  32-bit offset to allocated clause, or CLAUSE_REF_NULL on OOM.
 */
ClauseRef clause_array_allocate(ClauseArray* ca,
                                const int32_t* literals,
                                int size,
                                bool is_learned,
                                float activity);

/**
 * @brief Marks a clause as tombstoned (deleted) and tracks wasted words.
 *
 * @param ca   ClauseArray instance.
 * @param ref  Clause reference offset.
 */
void clause_array_mark_deleted(ClauseArray* ca, ClauseRef ref);

/* ========================================================================= */
/* 4. Inlined Fast-Path Accessors for Hot BCP & Resolution Loops             */
/* ========================================================================= */

/**
 * @brief Returns the packed size of the clause.
 */
static inline int clause_array_get_size(const ClauseArray* ca, ClauseRef ref) {
    return (int)(ca->memory[ref] & CLAUSE_SIZE_MASK);
}

/**
 * @brief Returns whether the clause is a learned lemma.
 */
static inline bool clause_array_is_learned(const ClauseArray* ca, ClauseRef ref) {
    return (ca->memory[ref] & CLAUSE_LEARNED_FLAG) != 0;
}

/**
 * @brief Returns whether the clause has been tombstoned/deleted.
 */
static inline bool clause_array_is_deleted(const ClauseArray* ca, ClauseRef ref) {
    return (ca->memory[ref] & CLAUSE_DELETED_FLAG) != 0;
}

/**
 * @brief Retrieves the single-precision activity score.
 */
static inline float clause_array_get_activity(const ClauseArray* ca, ClauseRef ref) {
    float act;
    memcpy(&act, &ca->memory[ref + 1], sizeof(float));
    return act;
}

/**
 * @brief Sets the activity score of a learned clause.
 */
static inline void clause_array_set_activity(ClauseArray* ca, ClauseRef ref, float activity) {
    memcpy(&ca->memory[ref + 1], &activity, sizeof(float));
}

/**
 * @brief Direct pointer to the contiguous array of signed literals.
 */
static inline int32_t* clause_array_get_literals(ClauseArray* ca, ClauseRef ref) {
    return (int32_t*)&ca->memory[ref + CLAUSE_HEADER_WORDS];
}

/**
 * @brief Read-only direct pointer to the contiguous array of signed literals.
 */
static inline const int32_t* clause_array_get_literals_const(const ClauseArray* ca, ClauseRef ref) {
    return (const int32_t*)&ca->memory[ref + CLAUSE_HEADER_WORDS];
}

/**
 * @brief Retrieves the literal at position index (0 <= index < size).
 */
static inline int32_t clause_array_get_literal(const ClauseArray* ca, ClauseRef ref, int index) {
    return (int32_t)ca->memory[ref + CLAUSE_HEADER_WORDS + (size_t)index];
}

/**
 * @brief Updates the literal at position index (e.g. 2-WL watch replacement).
 */
static inline void clause_array_set_literal(ClauseArray* ca, ClauseRef ref, int index, int32_t lit) {
    ca->memory[ref + CLAUSE_HEADER_WORDS + (size_t)index] = (uint32_t)lit;
}

/**
 * @brief Swaps two literal positions within a clause (used by 2-WL to keep watches at [0] and [1]).
 */
static inline void clause_array_swap_literals(ClauseArray* ca, ClauseRef ref, int idx_a, int idx_b) {
    uint32_t* lits = &ca->memory[ref + CLAUSE_HEADER_WORDS];
    uint32_t tmp = lits[idx_a];
    lits[idx_a] = lits[idx_b];
    lits[idx_b] = tmp;
}

/* ========================================================================= */
/* 5. Two-Watched-Literal (2-WL) Helper Functions                            */
/* ========================================================================= */

/**
 * @brief Initializes a WatchList array.
 */
bool watch_list_init(WatchList* wl, uint32_t initial_capacity);

/**
 * @brief Appends a watcher entry with inline blocking literal.
 */
bool watch_list_push(WatchList* wl, ClauseRef ref, int32_t blocking_literal);

/**
 * @brief Releases memory allocated for a WatchList.
 */
void watch_list_destroy(WatchList* wl);

/**
 * @brief Clears all watchers in a WatchList without freeing the buffer.
 */
static inline void watch_list_clear(WatchList* wl) {
    if (wl) wl->size = 0;
}

/* ========================================================================= */
/* 6. In-Place Compaction & Database Reduction API                           */
/* ========================================================================= */

/**
 * @brief Callback signature invoked during compaction to remap clause references.
 *
 * @param old_ref Old ClauseRef before compaction.
 * @param new_ref New ClauseRef after sliding compaction.
 * @param ctx     User context pointer (e.g. pointer to CDCLSolver for watch list remap).
 */
typedef void (*ClauseRemapCallback)(ClauseRef old_ref, ClauseRef new_ref, void* ctx);

/**
 * @brief Checks if wasted space exceeds the compaction threshold (e.g. 0.30 = 30%).
 */
bool clause_array_compaction_needed(const ClauseArray* ca, double wasted_threshold);

/**
 * @brief Performs in-place sliding compaction, removing deleted clauses and
 *        invoking remap_cb to update external watcher and reason references.
 *
 * @param ca        ClauseArray instance.
 * @param remap_cb  Callback to inform solver of shifted clause references (safe with NULL).
 * @param ctx       Context pointer forwarded to remap_cb.
 */
void clause_array_compact(ClauseArray* ca, ClauseRemapCallback remap_cb, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* SODIUM_CLAUSE_ARRAY_H */
