#include "CDCL.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#if defined(_WIN32)
#include <windows.h>
static inline double cdcl_get_time_ms(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return ((double)count.QuadPart * 1000.0) / (double)freq.QuadPart;
}
#else
#include <time.h>
static inline double cdcl_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
}
#endif

bool ENQUEUE(CDCLSolver* solver, int literal, ClauseRef reason_clause);
void ATTACH_CLAUSE(CDCLSolver* solver, ClauseRef ref);
void DESTROY_SOLVER(CDCLSolver* solver);
int INITIALIZE_REASONING_SUBSYSTEMS(CDCLSolver* solver, int variables);
void UPDATE_BRANCHING_HEURISTIC(CDCLSolver* solver);
int REFRESH_CLAUSE_DATABASE(CDCLSolver* solver);
void COMPACT_CLAUSE_DATABASE(CDCLSolver* solver);

static inline void CLEANUP_SOLVER(CDCLSolver* solver) {
    DESTROY_SOLVER(solver);
}

static inline int INITIALIZE_BASIC_SOLVER_STATE(CDCLSolver* solver, int variables) {
    if (variables < 0 || variables > SODIUM_MAX_VARIABLES) {
        return FAILURE;
    }
    solver->number_of_variables = variables;
    solver->assignment = (AssignmentValue*)malloc(((size_t)variables + 1) * sizeof(AssignmentValue));
    solver->decision_level = (int*)malloc(((size_t)variables + 1) * sizeof(int));
    solver->reason = (ClauseRef*)calloc((size_t)variables + 1, sizeof(ClauseRef));

    if (!solver->assignment || !solver->decision_level || !solver->reason) {
        if (solver->assignment) { free(solver->assignment); solver->assignment = NULL; }
        if (solver->decision_level) { free(solver->decision_level); solver->decision_level = NULL; }
        if (solver->reason) { free(solver->reason); solver->reason = NULL; }
        return FAILURE;
    }

    for (int i = 1; i <= variables; i++) {
        solver->assignment[i] = UNASSIGNED;
        solver->decision_level[i] = 0;
        solver->reason[i] = CLAUSE_REF_NULL;
    }

    solver->current_level = 0;
    solver->propagation_head = 0;
    solver->solver_mode = NORMAL_SEARCH;

    solver->conflict_count = 0;
    solver->conflicts_since_restart = 0;
    solver->restart_base = 100;
    solver->restart_limit = solver->restart_base;
    solver->restart_index = 1;
    solver->learned_clause_count = 0;
    solver->min_learned_clauses = 100;
    solver->refresh_interval = 1000;
    solver->next_refresh_conflict = solver->refresh_interval;
    return SUCCESS;
}

static inline void ADD_ORIGINAL_CLAUSE(CDCLSolver* solver, Clause* clause) {
    ClauseRef ref = clause_array_allocate(&solver->arena, clause->literals, clause->size, clause->is_learned, (float)clause->activity);
    if (solver->clause_count >= solver->clause_capacity) {
        solver->clause_capacity = (solver->clause_capacity == 0) ? 16 : solver->clause_capacity * 2;
        solver->clauses = (ClauseRef*)realloc(solver->clauses, solver->clause_capacity * sizeof(ClauseRef));
    }
    solver->clauses[solver->clause_count++] = ref;
    ATTACH_CLAUSE(solver, ref);
}

static inline SolverMode INITIALIZE_UNIT_PROPAGATION(CDCLSolver* solver) {
    for (int i = 0; i < solver->clause_count; i++) {
        ClauseRef ref = solver->clauses[i];
        if (ref != CLAUSE_REF_NULL && clause_array_get_size(&solver->arena, ref) == 1) {
            int lit = clause_array_get_literal(&solver->arena, ref, 0);
            if (!ENQUEUE(solver, lit, ref)) {
                return UNSAT;
            }
        }
    }
    return (SolverMode)SUCCESS;
}

SolverMode INITIALIZE_SOLVER(CDCLSolver* solver, int variables, Clause** input_clauses, int input_clause_count) {
    memset(solver, 0, sizeof(*solver));
    if (INITIALIZE_BASIC_SOLVER_STATE(solver, variables) != SUCCESS) {
        CLEANUP_SOLVER(solver);
        return (SolverMode)FAILURE;
    }
    if (INITIALIZE_REASONING_SUBSYSTEMS(solver, variables) == FAILURE) {
        CLEANUP_SOLVER(solver);
        return (SolverMode)FAILURE;
    }

    for (int i = 0; i < input_clause_count; i++) {
        Clause* clause = input_clauses[i];

        if (clause == NULL || clause->size == 0) {
            solver->solver_mode = UNSAT;
            CLEANUP_SOLVER(solver);
            return UNSAT;
        }

        ADD_ORIGINAL_CLAUSE(solver, clause);
    }

    if (INITIALIZE_UNIT_PROPAGATION(solver) == UNSAT) {
        solver->solver_mode = UNSAT;
        CLEANUP_SOLVER(solver);
        return UNSAT;
    }

    return (SolverMode)SUCCESS;
}

void ATTACH_CLAUSE(CDCLSolver* solver, ClauseRef ref) {
    if (ref == CLAUSE_REF_NULL || clause_array_is_deleted(&solver->arena, ref)) {
        return;
    }

    int size = clause_array_get_size(&solver->arena, ref);
    if (size <= 1) {
        if (size == 1 && solver->assignment) {
            int lit = clause_array_get_literal(&solver->arena, ref, 0);
            if (solver->assignment[abs(lit)] == UNASSIGNED) {
                ENQUEUE(solver, lit, ref);
            }
        }
        return;
    }

    int32_t* lits = clause_array_get_literals(&solver->arena, ref);
    int lit0 = lits[0];
    int lit1 = lits[1];

    size_t idx0 = LIT_TO_INDEX(lit0, solver->number_of_variables);
    watch_list_push(&solver->watch_lists[idx0], ref, lit1);

    size_t idx1 = LIT_TO_INDEX(lit1, solver->number_of_variables);
    watch_list_push(&solver->watch_lists[idx1], ref, lit0);
}

bool ENQUEUE(CDCLSolver* solver, int L, ClauseRef reason_clause) {
    int variable = abs(L);
    AssignmentValue required_value = (L > 0) ? TRUE : FALSE;

    if (solver->assignment[variable] != UNASSIGNED) {
        return (solver->assignment[variable] == required_value);
    }

    solver->assignment[variable] = required_value;
    solver->decision_level[variable] = solver->current_level;
    solver->reason[variable] = reason_clause;

    if (solver->trail_size >= solver->trail_capacity) {
        solver->trail_capacity = (solver->trail_capacity == 0) ? 16 : solver->trail_capacity * 2;
        solver->trail = (int*)realloc(solver->trail, solver->trail_capacity * sizeof(int));
    }
    solver->trail[solver->trail_size++] = L;

    return true;
}

static inline AssignmentValue LIT_VALUE(const CDCLSolver* solver, int lit) {
    int var = abs(lit);
    AssignmentValue val = solver->assignment[var];
    if (val == UNASSIGNED) return UNASSIGNED;
    if (lit > 0) return val;
    return (val == TRUE) ? FALSE : TRUE;
}

ClauseRef PROPAGATE(CDCLSolver* solver) {
    size_t n = solver->number_of_variables;
    while (solver->propagation_head < solver->trail_size) {
        int assigned_literal = solver->trail[solver->propagation_head++];
        int false_literal = -assigned_literal;

        size_t false_idx = LIT_TO_INDEX(false_literal, n);
        WatchList* wl = &solver->watch_lists[false_idx];
        Watcher* watchers = wl->watchers;

        uint32_t read_index = 0;
        uint32_t write_index = 0;

        while (read_index < wl->size) {
            Watcher w = watchers[read_index++];
            ClauseRef ref = w.clause_ref;

            if (clause_array_is_deleted(&solver->arena, ref)) {
                continue;
            }

            // Fast Path 1: Check blocking literal without touching the arena memory
            if (LIT_VALUE(solver, w.blocking_literal) == TRUE) {
                watchers[write_index++] = w;
                continue;
            }

            int32_t* lits = clause_array_get_literals(&solver->arena, ref);
            int size = clause_array_get_size(&solver->arena, ref);

            // Ensure false_literal is at lits[0]
            if (lits[1] == false_literal) {
                clause_array_swap_literals(&solver->arena, ref, 0, 1);
            }

            if (lits[0] != false_literal) {
                continue;
            }

            int other_watch = lits[1];

            // Fast Path 2: other watch already satisfies clause
            if (LIT_VALUE(solver, other_watch) == TRUE) {
                w.blocking_literal = other_watch;
                watchers[write_index++] = w;
                continue;
            }

            // Search for replacement watch
            bool replacement_found = false;
            for (int pos = 2; pos < size; pos++) {
                int candidate = lits[pos];
                if (LIT_VALUE(solver, candidate) != FALSE) {
                    clause_array_swap_literals(&solver->arena, ref, 0, pos);

                    size_t cand_idx = LIT_TO_INDEX(candidate, n);
                    watch_list_push(&solver->watch_lists[cand_idx], ref, other_watch);

                    replacement_found = true;
                    break;
                }
            }

            if (replacement_found) {
                continue;
            }

            // No replacement found: clause remains in current watch list
            w.blocking_literal = other_watch;
            watchers[write_index++] = w;

            // Conflict detected: other watch is FALSE
            if (LIT_VALUE(solver, other_watch) == FALSE) {
                while (read_index < wl->size) {
                    watchers[write_index++] = watchers[read_index++];
                }
                wl->size = write_index;
                return ref;
            }

            // Unit propagation: other watch is UNASSIGNED
            if (!ENQUEUE(solver, other_watch, ref)) {
                while (read_index < wl->size) {
                    watchers[write_index++] = watchers[read_index++];
                }
                wl->size = write_index;
                return ref;
            }
        }

        wl->size = write_index;
    }

    return CLAUSE_REF_NULL;
}

#define NO_POSITION -1

static inline void learned_literals_push(CDCLSolver* solver, int* size, int lit) {
    if (*size >= solver->learned_literals_capacity) {
        solver->learned_literals_capacity = (solver->learned_literals_capacity == 0) ? 32 : solver->learned_literals_capacity * 2;
        solver->learned_literals = (int*)realloc(solver->learned_literals,
            solver->learned_literals_capacity * sizeof(int));
    }
    solver->learned_literals[(*size)++] = lit;
}

int ANALYZE_CONFLICT(CDCLSolver* solver, ClauseRef conflict_clause, ClauseRef* out_learned_clause) {
    solver->solver_mode = CONFLICT_ANALYSIS;

    int learned_size = 0;
    learned_literals_push(solver, &learned_size, 0); // Placeholder for index 0 (asserting literal)

    for (int i = 0; i < solver->bump_variables_count; i++) {
        solver->seen[solver->bump_variables[i]] = false;
    }
    solver->bump_variables_count = 0;

    int path_count = 0;
    int trail_index = solver->trail_size - 1;
    ClauseRef current_clause = conflict_clause;
    int pivot_var = 0;

    while (true) {
        if (clause_array_is_learned(&solver->arena, current_clause)) {
            float act = clause_array_get_activity(&solver->arena, current_clause);
            clause_array_set_activity(&solver->arena, current_clause, act + 1.0f);
        }

        int c_size = clause_array_get_size(&solver->arena, current_clause);
        int32_t* c_lits = clause_array_get_literals(&solver->arena, current_clause);

        for (int i = 0; i < c_size; i++) {
            int L = c_lits[i];
            int variable = abs(L);

            if (variable == pivot_var || solver->seen[variable] || solver->decision_level[variable] == 0) {
                continue;
            }

            solver->seen[variable] = true;

            if (solver->bump_variables_count >= solver->bump_variables_capacity) {
                solver->bump_variables_capacity *= 2;
                solver->bump_variables = (int*)realloc(solver->bump_variables,
                    solver->bump_variables_capacity * sizeof(int));
            }
            solver->bump_variables[solver->bump_variables_count++] = variable;

            if (solver->decision_level[variable] == solver->current_level) {
                path_count++;
            } else {
                learned_literals_push(solver, &learned_size, L);
            }
        }

        while (trail_index >= 0 && solver->seen[abs(solver->trail[trail_index])] == false) {
            trail_index--;
        }

        if (trail_index < 0) {
            for (int i = 0; i < solver->bump_variables_count; i++) {
                solver->seen[solver->bump_variables[i]] = false;
            }
            *out_learned_clause = CLAUSE_REF_NULL;
            return ANALYSIS_ERROR;
        }

        int resolved_literal = solver->trail[trail_index];
        int variable = abs(resolved_literal);
        trail_index--;

        solver->seen[variable] = false;
        path_count--;

        if (path_count == 0) {
            solver->learned_literals[0] = -resolved_literal;
            break;
        }

        ClauseRef reason_clause = solver->reason[variable];
        if (reason_clause == CLAUSE_REF_NULL) {
            for (int i = 0; i < solver->bump_variables_count; i++) {
                solver->seen[solver->bump_variables[i]] = false;
            }
            *out_learned_clause = CLAUSE_REF_NULL;
            return ANALYSIS_ERROR;
        }

        pivot_var = variable;
        current_clause = reason_clause;
    }

    int write_idx = 1;
    for (int i = 1; i < learned_size; i++) {
        int lit = solver->learned_literals[i];
        bool duplicate = false;
        for (int j = 1; j < write_idx; j++) {
            if (solver->learned_literals[j] == lit) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            solver->learned_literals[write_idx++] = lit;
        }
    }
    learned_size = write_idx;

    int backtrack_level = 0;
    int highest_level_position = NO_POSITION;

    for (int pos = 1; pos < learned_size; pos++) {
        int L = solver->learned_literals[pos];
        int level = solver->decision_level[abs(L)];

        if (level > backtrack_level) {
            backtrack_level = level;
            highest_level_position = pos;
        }
    }

    if (highest_level_position != NO_POSITION) {
        int tmp = solver->learned_literals[1];
        solver->learned_literals[1] = solver->learned_literals[highest_level_position];
        solver->learned_literals[highest_level_position] = tmp;
    }

    for (int i = 0; i < solver->bump_variables_count; i++) {
        solver->seen[solver->bump_variables[i]] = false;
    }

    ClauseRef res_clause = clause_array_allocate(&solver->arena, solver->learned_literals, learned_size, true, 1.0f);

    *out_learned_clause = res_clause;
    return backtrack_level;
}

int BACKTRACK(CDCLSolver* solver, int target_level) {
    if (target_level > solver->current_level) {
        fprintf(stderr, "Error: Invalid backtrack target %d > current level %d\n",
                target_level, solver->current_level);
        return ERROR;
    }

    while (solver->current_level > target_level) {
        if (solver->trail_limits_size <= 0) {
            fprintf(stderr, "Error: Inconsistent decision-level state, trail_limits is empty\n");
            return ERROR;
        }

        int trail_start = solver->trail_limits[--solver->trail_limits_size];

        while (solver->trail_size > trail_start) {
            int L = solver->trail[--solver->trail_size];
            int variable = abs(L);

            if (solver->saved_polarity) {
                solver->saved_polarity[variable] = solver->assignment[variable];
            }

            solver->assignment[variable] = UNASSIGNED;
            solver->decision_level[variable] = 0;
            solver->reason[variable] = CLAUSE_REF_NULL;
        }

        solver->current_level--;
    }

    if (solver->propagation_head > solver->trail_size) {
        solver->propagation_head = solver->trail_size;
    }

    return SUCCESS;
}

#define MAX_RESTART_LIMIT (INT_MAX / 2)

int COMPUTE_RESTART_LIMIT(int base, int restart_index) {
    double calculated = (double)base * pow(1.5, (double)restart_index);
    if (calculated >= (double)MAX_RESTART_LIMIT) {
        return MAX_RESTART_LIMIT;
    }
    return (int)calculated;
}

int RESTART(CDCLSolver* solver) {
    if (BACKTRACK(solver, 0) == ERROR) {
        return ERROR;
    }

    if (clause_array_compaction_needed(&solver->arena, 0.20)) {
        COMPACT_CLAUSE_DATABASE(solver);
    }

    solver->conflicts_since_restart = 0;
    solver->restart_index++;

    solver->restart_limit = COMPUTE_RESTART_LIMIT(
        solver->restart_base,
        solver->restart_index
    );

    return SUCCESS;
}

static inline bool IS_RESTART_DUE(const CDCLSolver* solver) {
    return solver->conflicts_since_restart >= solver->restart_limit;
}

bool IS_DATABASE_REFRESH_DUE(const CDCLSolver* solver) {
    if (solver->conflict_count < solver->next_refresh_conflict) {
        return false;
    }
    if (solver->learned_clause_count < solver->min_learned_clauses) {
        return false;
    }
    return true;
}

int HANDLE_CONFLICT(CDCLSolver* solver, ClauseRef conflict_clause) {
    if (solver->current_level == 0) {
        return UNSAT;
    }

    ClauseRef learned_clause = CLAUSE_REF_NULL;
    int backtrack_level = ANALYZE_CONFLICT(solver, conflict_clause, &learned_clause);

    if (backtrack_level == ANALYSIS_ERROR || learned_clause == CLAUSE_REF_NULL) {
        return ERROR;
    }

    UPDATE_BRANCHING_HEURISTIC(solver);

    if (BACKTRACK(solver, backtrack_level) == ERROR) {
        return ERROR;
    }

    if (solver->clause_count >= solver->clause_capacity) {
        solver->clause_capacity = (solver->clause_capacity == 0) ? 16 : solver->clause_capacity * 2;
        solver->clauses = (ClauseRef*)realloc(solver->clauses, solver->clause_capacity * sizeof(ClauseRef));
    }
    solver->clauses[solver->clause_count++] = learned_clause;
    solver->learned_clause_count++;

    ATTACH_CLAUSE(solver, learned_clause);

    int asserting_literal = clause_array_get_literal(&solver->arena, learned_clause, 0);

    if (!ENQUEUE(solver, asserting_literal, learned_clause)) {
        return ERROR;
    }

    solver->conflict_count += 1;
    solver->conflicts_since_restart += 1;

    if (IS_DATABASE_REFRESH_DUE(solver)) {
        REFRESH_CLAUSE_DATABASE(solver);
    }

    if (IS_RESTART_DUE(solver)) {
        RESTART(solver);
    }

    return CONTINUE;
}

AssignmentValue CHOOSE_POLARITY(CDCLSolver* solver, int variable) {
    if (solver->saved_polarity && solver->saved_polarity[variable] != UNDEFINED) {
        return solver->saved_polarity[variable];
    }
    return VAL_TRUE;
}

static inline SolverMode CDCL_SEARCH_LOOP(CDCLSolver* solver) {
    uint32_t step_counter = 0;
    while (true) {
        // Watchdog & Interruption Check (checked every 512 loop iterations)
        if ((++step_counter & 0x1FF) == 0) {
            if (solver->interrupt_flag && *solver->interrupt_flag) {
                solver->solver_mode = SOLVER_INTERRUPTED;
                return SOLVER_INTERRUPTED;
            }
            if (solver->deadline_ms > 0.0 && cdcl_get_time_ms() >= solver->deadline_ms) {
                solver->solver_mode = SOLVER_TIMEOUT;
                return SOLVER_TIMEOUT;
            }
            if (solver->max_conflicts > 0 && (uint64_t)solver->conflict_count >= solver->max_conflicts) {
                solver->solver_mode = SOLVER_TIMEOUT;
                return SOLVER_TIMEOUT;
            }
        }

        ClauseRef conflict_clause = PROPAGATE(solver);

        if (conflict_clause != CLAUSE_REF_NULL) {
            int handle_res = HANDLE_CONFLICT(solver, conflict_clause);

            if (handle_res == UNSAT) {
                return UNSAT;
            }

            if (handle_res != CONTINUE && handle_res != SUCCESS) {
                return (SolverMode)ERROR;
            }

            continue;
        }

        int* assignment_ints = (int*)solver->assignment;
        size_t next_var_size_t = vsids_select_variable(&solver->vsids, assignment_ints);
        int next_variable = (next_var_size_t == NO_VARIABLE) ? 0 : (int)next_var_size_t;

        if (next_variable == 0) {
            solver->solver_mode = SAT;
            return SAT;
        }

        AssignmentValue pol = CHOOSE_POLARITY(solver, next_variable);
        int decision_literal = (pol == VAL_TRUE) ? next_variable : -next_variable;

        solver->current_level++;
        if (solver->trail_limits_size >= solver->trail_limits_capacity) {
            solver->trail_limits_capacity *= 2;
            solver->trail_limits = (int*)realloc(solver->trail_limits,
                solver->trail_limits_capacity * sizeof(int));
        }
        solver->trail_limits[solver->trail_limits_size++] = solver->trail_size;

        if (!ENQUEUE(solver, decision_literal, CLAUSE_REF_NULL)) {
            fprintf(stderr, "Invariant violation: Failed to enqueue decision literal %d\n", decision_literal);
            return (SolverMode)ERROR;
        }
    }
}

SolverMode INITIALIZE_SOLVER_FROM_ARENA(CDCLSolver* solver, int variables) {
    ClauseArray saved_arena = solver->arena;
    volatile sig_atomic_t* saved_interrupt = solver->interrupt_flag;
    double saved_deadline = solver->deadline_ms;
    uint64_t saved_max_conflicts = solver->max_conflicts;

    memset(solver, 0, sizeof(*solver));
    solver->arena = saved_arena;
    solver->interrupt_flag = saved_interrupt;
    solver->deadline_ms = saved_deadline;
    solver->max_conflicts = saved_max_conflicts;

    if (INITIALIZE_BASIC_SOLVER_STATE(solver, variables) != SUCCESS) {
        CLEANUP_SOLVER(solver);
        return (SolverMode)FAILURE;
    }
    if (INITIALIZE_REASONING_SUBSYSTEMS(solver, variables) == FAILURE) {
        CLEANUP_SOLVER(solver);
        return (SolverMode)FAILURE;
    }

    size_t offset = 1;
    while (offset < solver->arena.used_words) {
        uint32_t header = solver->arena.memory[offset];
        int size = (int)(header & CLAUSE_SIZE_MASK);
        ClauseRef ref = (ClauseRef)offset;

        if (size == 0) {
            solver->solver_mode = UNSAT;
            CLEANUP_SOLVER(solver);
            return UNSAT;
        }

        if (solver->clause_count >= solver->clause_capacity) {
            solver->clause_capacity = (solver->clause_capacity == 0) ? 16 : solver->clause_capacity * 2;
            solver->clauses = (ClauseRef*)realloc(solver->clauses, solver->clause_capacity * sizeof(ClauseRef));
        }
        solver->clauses[solver->clause_count++] = ref;
        if (clause_array_is_learned(&solver->arena, ref)) {
            solver->learned_clause_count++;
        }
        ATTACH_CLAUSE(solver, ref);

        offset += (size_t)CLAUSE_HEADER_WORDS + (size_t)size;
    }

    if (INITIALIZE_UNIT_PROPAGATION(solver) == UNSAT) {
        solver->solver_mode = UNSAT;
        CLEANUP_SOLVER(solver);
        return UNSAT;
    }

    return (SolverMode)SUCCESS;
}

SolverMode SOLVE(CDCLSolver* solver, int number_of_variables, Clause** input_clauses, int input_clause_count) {
    SolverMode result = INITIALIZE_SOLVER(
        solver,
        number_of_variables,
        input_clauses,
        input_clause_count
    );

    if (result == UNSAT) {
        return UNSAT;
    }

    if (result != (SolverMode)SUCCESS && result != READY) {
        return (SolverMode)ERROR;
    }

    return CDCL_SEARCH_LOOP(solver);
}

SolverMode SOLVE_ARENA(CDCLSolver* solver, int number_of_variables) {
    SolverMode result = INITIALIZE_SOLVER_FROM_ARENA(solver, number_of_variables);

    if (result == UNSAT) {
        return UNSAT;
    }

    if (result != (SolverMode)SUCCESS && result != READY) {
        return (SolverMode)ERROR;
    }

    return CDCL_SEARCH_LOOP(solver);
}

static inline void remove_clause_from_watchlist(CDCLSolver* solver, ClauseRef ref, int lit) {
    size_t idx = LIT_TO_INDEX(lit, solver->number_of_variables);
    WatchList* wl = &solver->watch_lists[idx];
    for (uint32_t i = 0; i < wl->size; i++) {
        if (wl->watchers[i].clause_ref == ref) {
            wl->watchers[i] = wl->watchers[wl->size - 1];
            wl->size--;
            break;
        }
    }
}

static inline bool is_clause_a_reason(const CDCLSolver* solver, ClauseRef ref) {
    for (int i = 1; i <= solver->number_of_variables; i++) {
        if (solver->assignment[i] != UNASSIGNED && solver->reason[i] == ref) {
            return true;
        }
    }
    return false;
}

typedef struct {
    ClauseRef ref;
    float activity;
    int size;
} CandidateRef;

static int compare_candidate_refs(const void* a, const void* b) {
    const CandidateRef* ca = (const CandidateRef*)a;
    const CandidateRef* cb = (const CandidateRef*)b;
    if (ca->activity < cb->activity) return -1;
    if (ca->activity > cb->activity) return 1;
    if (ca->size > cb->size) return -1;
    if (ca->size < cb->size) return 1;
    return 0;
}

static void solver_remap_reasons_cb(ClauseRef old_ref, ClauseRef new_ref, void* ctx) {
    CDCLSolver* solver = (CDCLSolver*)ctx;
    for (int i = 1; i <= solver->number_of_variables; i++) {
        if (solver->reason[i] == old_ref) {
            solver->reason[i] = new_ref;
        }
    }
}

static void normalize_watches_for_attach(CDCLSolver* solver, ClauseRef ref) {
    int size = clause_array_get_size(&solver->arena, ref);
    if (size <= 2) return;
    int32_t* lits = clause_array_get_literals(&solver->arena, ref);

    if (LIT_VALUE(solver, lits[0]) == FALSE) {
        for (int i = 2; i < size; i++) {
            if (LIT_VALUE(solver, lits[i]) != FALSE) {
                int32_t tmp = lits[0];
                lits[0] = lits[i];
                lits[i] = tmp;
                break;
            }
        }
    }

    if (LIT_VALUE(solver, lits[1]) == FALSE) {
        for (int i = 2; i < size; i++) {
            if (LIT_VALUE(solver, lits[i]) != FALSE) {
                int32_t tmp = lits[1];
                lits[1] = lits[i];
                lits[i] = tmp;
                break;
            }
        }
    }
}

void COMPACT_CLAUSE_DATABASE(CDCLSolver* solver) {
    if (!solver || solver->arena.wasted_words == 0) {
        return;
    }

    clause_array_compact(&solver->arena, solver_remap_reasons_cb, solver);

    solver->clause_count = 0;
    solver->learned_clause_count = 0;
    size_t offset = 1;
    while (offset < solver->arena.used_words) {
        uint32_t header = solver->arena.memory[offset];
        int size = (int)(header & CLAUSE_SIZE_MASK);
        ClauseRef ref = (ClauseRef)offset;

        if (solver->clause_count >= solver->clause_capacity) {
            solver->clause_capacity = (solver->clause_capacity == 0) ? 16 : solver->clause_capacity * 2;
            solver->clauses = (ClauseRef*)realloc(solver->clauses, solver->clause_capacity * sizeof(ClauseRef));
        }
        solver->clauses[solver->clause_count++] = ref;
        if (clause_array_is_learned(&solver->arena, ref)) {
            solver->learned_clause_count++;
        }

        offset += (size_t)CLAUSE_HEADER_WORDS + (size_t)size;
    }

    int watch_list_total = 2 * solver->number_of_variables + 1;
    for (int i = 0; i < watch_list_total; i++) {
        watch_list_clear(&solver->watch_lists[i]);
    }

    for (int i = 0; i < solver->clause_count; i++) {
        ClauseRef ref = solver->clauses[i];
        normalize_watches_for_attach(solver, ref);
        ATTACH_CLAUSE(solver, ref);
    }
}

int REFRESH_CLAUSE_DATABASE(CDCLSolver* solver) {
    int candidate_cap = 64;
    CandidateRef* candidates = (CandidateRef*)malloc(candidate_cap * sizeof(CandidateRef));
    int candidate_count = 0;

    for (int i = 0; i < solver->clause_count; i++) {
        ClauseRef ref = solver->clauses[i];
        if (ref == CLAUSE_REF_NULL || !clause_array_is_learned(&solver->arena, ref)) {
            continue;
        }
        if (clause_array_is_deleted(&solver->arena, ref)) {
            continue;
        }
        int size = clause_array_get_size(&solver->arena, ref);
        if (size <= 1) {
            continue;
        }
        if (is_clause_a_reason(solver, ref)) {
            continue;
        }

        if (candidate_count >= candidate_cap) {
            candidate_cap *= 2;
            candidates = (CandidateRef*)realloc(candidates, candidate_cap * sizeof(CandidateRef));
        }
        candidates[candidate_count].ref = ref;
        candidates[candidate_count].activity = clause_array_get_activity(&solver->arena, ref);
        candidates[candidate_count].size = size;
        candidate_count++;
    }

    qsort(candidates, candidate_count, sizeof(CandidateRef), compare_candidate_refs);

    int to_delete_count = candidate_count / 2;
    for (int i = 0; i < to_delete_count; i++) {
        ClauseRef ref = candidates[i].ref;
        int lit0 = clause_array_get_literal(&solver->arena, ref, 0);
        int lit1 = clause_array_get_literal(&solver->arena, ref, 1);

        remove_clause_from_watchlist(solver, ref, lit0);
        remove_clause_from_watchlist(solver, ref, lit1);

        clause_array_mark_deleted(&solver->arena, ref);
    }
    free(candidates);

    int write_idx = 0;
    for (int i = 0; i < solver->clause_count; i++) {
        ClauseRef ref = solver->clauses[i];
        if (ref != CLAUSE_REF_NULL && clause_array_is_deleted(&solver->arena, ref)) {
            solver->learned_clause_count--;
        } else {
            solver->clauses[write_idx++] = ref;
        }
    }
    solver->clause_count = write_idx;

    if (solver->current_level == 0 && solver->arena.wasted_words > 0) {
        COMPACT_CLAUSE_DATABASE(solver);
    }

    solver->next_refresh_conflict = solver->conflict_count + solver->refresh_interval;
    return SUCCESS;
}

int INITIALIZE_REASONING_SUBSYSTEMS(CDCLSolver* solver, int variables) {
    if (variables < 0 || variables > SODIUM_MAX_VARIABLES) {
        return FAILURE;
    }

    MemoryManager* mm_ptr = NULL;
    if (initialize_memory_manager(variables + 64, &mm_ptr) != STATUS_SUCCESS || !mm_ptr) {
        return FAILURE;
    }
    solver->memory_manager = *mm_ptr;
    free(mm_ptr);
    if (!vsids_initialize_with_manager(&solver->vsids, &solver->memory_manager, variables, DEFAULT_DECAY_FACTOR)) {
        return FAILURE;
    }

    if (!solver->arena.memory) {
        if (!clause_array_init(&solver->arena, 0)) {
            return FAILURE;
        }
    }

    solver->trail_capacity = variables > 0 ? variables : 16;
    solver->trail = (int*)malloc((size_t)solver->trail_capacity * sizeof(int));
    if (solver->trail == NULL) {
        return FAILURE;
    }
    solver->trail_size = 0;

    solver->trail_limits_capacity = 32;
    solver->trail_limits = (int*)malloc((size_t)solver->trail_limits_capacity * sizeof(int));
    if (solver->trail_limits == NULL) {
        return FAILURE;
    }
    solver->trail_limits_size = 0;

    size_t watch_list_total = (size_t)2 * (size_t)variables + 1;
    if (watch_list_total > ((size_t)2 * SODIUM_MAX_VARIABLES + 1)) {
        return FAILURE;
    }
    solver->watch_lists = (WatchList*)malloc(watch_list_total * sizeof(WatchList));
    if (solver->watch_lists == NULL) {
        return FAILURE;
    }
    for (size_t i = 0; i < watch_list_total; i++) {
        if (!watch_list_init(&solver->watch_lists[i], 4)) {
            return FAILURE;
        }
    }

    solver->clause_capacity = 64;
    solver->clauses = (ClauseRef*)malloc((size_t)solver->clause_capacity * sizeof(ClauseRef));
    if (solver->clauses == NULL) {
        return FAILURE;
    }
    solver->clause_count = 0;

    solver->seen = (bool*)calloc((size_t)variables + 1, sizeof(bool));
    if (solver->seen == NULL) {
        return FAILURE;
    }

    solver->bump_variables_capacity = 32;
    solver->bump_variables = (int*)malloc((size_t)solver->bump_variables_capacity * sizeof(int));
    if (solver->bump_variables == NULL) {
        return FAILURE;
    }
    solver->bump_variables_count = 0;

    solver->saved_polarity = (AssignmentValue*)malloc(((size_t)variables + 1) * sizeof(AssignmentValue));
    if (solver->saved_polarity == NULL) {
        return FAILURE;
    }
    for (int i = 1; i <= variables; i++) {
        solver->saved_polarity[i] = UNASSIGNED;
    }

    solver->learned_literals_capacity = 32;
    solver->learned_literals = (int*)malloc((size_t)solver->learned_literals_capacity * sizeof(int));
    if (solver->learned_literals == NULL) {
        return FAILURE;
    }

    return SUCCESS;
}

void UPDATE_BRANCHING_HEURISTIC(CDCLSolver* solver) {
    for (int i = 0; i < solver->bump_variables_count; i++) {
        vsids_bump(&solver->vsids, (size_t)solver->bump_variables[i]);
    }
    vsids_decay(&solver->vsids);
}

int SELECT_BRANCH_VARIABLE(CDCLSolver* solver) {
    size_t var = vsids_select_variable(&solver->vsids, (const int*)solver->assignment);
    return (var == NO_VARIABLE) ? 0 : (int)var;
}

void DESTROY_SOLVER(CDCLSolver* solver) {
    if (!solver) return;

    if (solver->assignment) {
        free(solver->assignment);
        solver->assignment = NULL;
    }
    if (solver->saved_polarity) {
        free(solver->saved_polarity);
        solver->saved_polarity = NULL;
    }
    if (solver->decision_level) {
        free(solver->decision_level);
        solver->decision_level = NULL;
    }
    if (solver->reason) {
        free(solver->reason);
        solver->reason = NULL;
    }
    if (solver->trail) {
        free(solver->trail);
        solver->trail = NULL;
    }
    solver->trail_size = 0;
    solver->trail_capacity = 0;

    if (solver->trail_limits) {
        free(solver->trail_limits);
        solver->trail_limits = NULL;
    }
    solver->trail_limits_size = 0;
    solver->trail_limits_capacity = 0;

    if (solver->watch_lists) {
        int watch_list_total = 2 * solver->number_of_variables + 1;
        for (int i = 0; i < watch_list_total; i++) {
            watch_list_destroy(&solver->watch_lists[i]);
        }
        free(solver->watch_lists);
        solver->watch_lists = NULL;
    }

    clause_array_destroy(&solver->arena);

    if (solver->seen) {
        free(solver->seen);
        solver->seen = NULL;
    }
    if (solver->bump_variables) {
        free(solver->bump_variables);
        solver->bump_variables = NULL;
    }
    solver->bump_variables_count = 0;
    solver->bump_variables_capacity = 0;

    if (solver->clauses) {
        free(solver->clauses);
        solver->clauses = NULL;
    }
    solver->clause_count = 0;
    solver->clause_capacity = 0;

    if (solver->learned_literals) {
        free(solver->learned_literals);
        solver->learned_literals = NULL;
    }
    solver->learned_literals_capacity = 0;

    vsids_cleanup(&solver->vsids);
    memset(&solver->vsids, 0, sizeof(solver->vsids));

    cleanup_memory_manager(&solver->memory_manager);
    memset(&solver->memory_manager, 0, sizeof(solver->memory_manager));

    solver->number_of_variables = 0;
    solver->current_level = 0;
    solver->propagation_head = 0;
    solver->solver_mode = NORMAL_SEARCH;
    solver->conflict_count = 0;
    solver->conflicts_since_restart = 0;
    solver->restart_limit = 0;
    solver->restart_base = 0;
    solver->restart_index = 0;
}
