#ifndef VSIDS_H
#define VSIDS_H

#include "memory.h"
#include <stdbool.h>
#include <stddef.h>

#define DEFAULT_DECAY_FACTOR 0.95
#define INITIAL_INCREMENT    1.0
#define RESCALE_THRESHOLD    1e100
#define RESCALE_DIVISOR      1e100

#define NO_VARIABLE          0
#define UNASSIGNED           0

typedef struct VSIDS {
    MemoryManager* manager;
    RecordHandle   activity_handle;
    size_t         variable_count;
    double         increment;
    double         decay_factor;
    bool           owns_manager;
} VSIDS;

/**
 * @brief Initializes VSIDS with default decay factor (0.95) and internal MemoryManager.
 */
bool vsids_initialize(VSIDS* vsids, size_t variable_count);

/**
 * @brief Initializes VSIDS with custom decay factor and internal MemoryManager.
 */
bool vsids_initialize_with_decay(VSIDS* vsids, size_t variable_count, double decay_factor);

/**
 * @brief Initializes VSIDS using an existing/shared MemoryManager.
 */
bool vsids_initialize_with_manager(VSIDS* vsids, MemoryManager* manager, size_t variable_count, double decay_factor);

/**
 * @brief Cleans up memory used by VSIDS and deletes the activity record.
 */
void vsids_destroy(VSIDS* vsids);

/**
 * @brief Alias for vsids_destroy.
 */
static inline void vsids_cleanup(VSIDS* vsids) {
    vsids_destroy(vsids);
}

/**
 * @brief Increases the activity score of a single variable.
 */
void vsids_bump_variable(VSIDS* vsids, size_t variable);

/**
 * @brief Shorthand alias for vsids_bump_variable.
 */
static inline void vsids_bump(VSIDS* vsids, size_t variable) {
    vsids_bump_variable(vsids, variable);
}

/**
 * @brief Increases the activity scores for a set of variables.
 */
void vsids_bump_variables(VSIDS* vsids, const size_t* variable_set, size_t set_size);

/**
 * @brief Decays activity scores by increasing the score increment.
 */
void vsids_decay_activities(VSIDS* vsids);

/**
 * @brief Shorthand alias for vsids_decay_activities.
 */
static inline void vsids_decay(VSIDS* vsids) {
    vsids_decay_activities(vsids);
}

/**
 * @brief Divides all activity scores to prevent floating-point overflow.
 */
void vsids_rescale_activities(VSIDS* vsids);

/**
 * @brief Selects the unassigned variable with the highest activity score.
 * @return Variable index (1..variable_count), or NO_VARIABLE (0) if all are assigned.
 */
size_t vsids_select_variable(const VSIDS* vsids, const int* assignment);

/**
 * @brief Retrieves the current activity score for a given variable.
 */
double vsids_get_activity(const VSIDS* vsids, size_t variable);

#endif // VSIDS_H
