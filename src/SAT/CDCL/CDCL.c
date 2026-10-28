#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "VSIDS.h"
#include "memory.h"

typedef enum {
    UNASSIGNED,
    TRUE,
    FALSE
} AssignmentValue;

typedef enum {
    NORMAL_SEARCH,
    CONFLICT_ANALYSIS,
    UNSAT,
    SAT,
    READY
} SolverMode;

#define NO_REASON NULL
#define LIT_TO_INDEX(lit, n) ((lit) > 0 ? (size_t)(lit) : (size_t)((n) + abs(lit)))

typedef struct {
    int* literals;       // Array of signed integers
    int size;            // Number of literals
    bool is_learned;     // Whether the clause was learned
    double activity;     // Usefulness score for database reduction
    bool is_deleted;     // Whether the clause has been removed
} Clause;

typedef struct {
    int number_of_variables;
    AssignmentValue* assignment;        // 1..number_of_variables
    AssignmentValue* saved_polarity;    // 1..number_of_variables (phase saving)
    int* decision_level;                // 1..number_of_variables
    Clause** reason;                    // 1..number_of_variables
    int* trail;                         // Assigned literals, in assignment order
    int trail_size;
    int trail_capacity;
    int* trail_limits;                  // Start index of each decision level
    int trail_limits_size;
    int trail_limits_capacity;
    int current_level;
    int propagation_head;               // Index of the next trail literal to propagate
    Clause*** watch_lists;              // One list per signed literal
    int* watch_list_sizes;
    int* watch_list_capacities;
    Clause** clauses;                   // Original and learned clause database
    int clause_count;
    int clause_capacity;
    bool* seen;                         // 1..number_of_variables
    int* bump_variables;
    int bump_variables_count;
    int bump_variables_capacity;
    SolverMode solver_mode;
    MemoryManager memory_manager;
    VSIDS vsids;
    int conflict_count;
    int conflicts_since_restart;
    int restart_limit;
    int restart_base;
    int restart_index;
    int learned_clause_count;
    int min_learned_clauses;
    int next_refresh_conflict;
    int refresh_interval;
} CDCLSolver;

// Forward declarations for helper procedures
bool ENQUEUE(CDCLSolver* solver, int literal, Clause* reason_clause);
void ATTACH_CLAUSE(CDCLSolver* solver, Clause* clause);
void DESTROY_SOLVER(CDCLSolver* solver);
int INITIALIZE_REASONING_SUBSYSTEMS(CDCLSolver* solver, int variables);

static inline void CLEANUP_SOLVER(CDCLSolver* solver) {
    DESTROY_SOLVER(solver);
}

static inline void INITIALIZE_BASIC_SOLVER_STATE(CDCLSolver* solver, int variables) {
    solver->number_of_variables = variables;
    solver->assignment = (AssignmentValue*)malloc((variables + 1) * sizeof(AssignmentValue));
    solver->decision_level = (int*)malloc((variables + 1) * sizeof(int));
    solver->reason = (Clause**)malloc((variables + 1) * sizeof(Clause*));

    for (int i = 1; i <= variables; i++) {
        solver->assignment[i] = UNASSIGNED;
        solver->decision_level[i] = 0;
        solver->reason[i] = NO_REASON;
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
}

static inline void ADD_ORIGINAL_CLAUSE(CDCLSolver* solver, Clause* clause) {
    if (solver->clause_count >= solver->clause_capacity) {
        solver->clause_capacity = (solver->clause_capacity == 0) ? 16 : solver->clause_capacity * 2;
        solver->clauses = (Clause**)realloc(solver->clauses, solver->clause_capacity * sizeof(Clause*));
    }
    solver->clauses[solver->clause_count++] = clause;
    ATTACH_CLAUSE(solver, clause);
}

static inline void INITIALIZE_UNIT_PROPAGATION(CDCLSolver* solver) {
    for (int i = 0; i < solver->clause_count; i++) {
        Clause* C = solver->clauses[i];
        if (C != NULL && C->size == 1) {
            ENQUEUE(solver, C->literals[0], C);
        }
    }
}

SolverMode INITIALIZE_SOLVER(CDCLSolver* solver, int variables, Clause** input_clauses, int input_clause_count) {
    INITIALIZE_BASIC_SOLVER_STATE(solver, variables);
    if (INITIALIZE_REASONING_SUBSYSTEMS(solver, variables) == FAILURE) {
        CLEANUP_SOLVER(solver);
        return (SolverMode)FAILURE;
    }

    // FOR EACH clause IN clauses:
    for (int i = 0; i < input_clause_count; i++) {
        Clause* clause = input_clauses[i];

        // IF clause is empty:
        //     CLEANUP_SOLVER(solver)
        //     RETURN UNSAT
        if (clause == NULL || clause->size == 0) {
            solver->solver_mode = UNSAT;
            CLEANUP_SOLVER(solver);
            return UNSAT;
        }

        // ADD_ORIGINAL_CLAUSE(solver, clause)
        ADD_ORIGINAL_CLAUSE(solver, clause);
    }

    // INITIALIZE_UNIT_PROPAGATION(solver)
    INITIALIZE_UNIT_PROPAGATION(solver);

    // RETURN SUCCESS
    return (SolverMode)SUCCESS;
}

void ATTACH_CLAUSE(CDCLSolver* solver, Clause* C) {
    // IF C is deleted:
    //     Return.
    if (C == NULL || C->is_deleted) {
        return;
    }

    // IF C contains exactly one literal:
    //     Handle it as a unit clause.
    //     Do not add it to a two-watch list.
    //     Return.
    if (C->size <= 1) {
        if (C->size == 1) {
            ENQUEUE(solver, C->literals[0], C);
        }
        return;
    }
    int lit0 = C->literals[0];
    size_t idx0 = LIT_TO_INDEX(lit0, solver->number_of_variables);
    if (solver->watch_list_sizes[idx0] >= solver->watch_list_capacities[idx0]) {
        solver->watch_list_capacities[idx0] *= 2;
        solver->watch_lists[idx0] = (Clause**)realloc(solver->watch_lists[idx0],
            solver->watch_list_capacities[idx0] * sizeof(Clause*));
    }
    solver->watch_lists[idx0][solver->watch_list_sizes[idx0]++] = C;

    // Add C to the watch list for C.literals[1].
    int lit1 = C->literals[1];
    size_t idx1 = LIT_TO_INDEX(lit1, solver->number_of_variables);
    if (solver->watch_list_sizes[idx1] >= solver->watch_list_capacities[idx1]) {
        solver->watch_list_capacities[idx1] *= 2;
        solver->watch_lists[idx1] = (Clause**)realloc(solver->watch_lists[idx1],
            solver->watch_list_capacities[idx1] * sizeof(Clause*));
    }
    solver->watch_lists[idx1][solver->watch_list_sizes[idx1]++] = C;
}

bool ENQUEUE(CDCLSolver* solver, int L, Clause* reason_clause) {
    int variable = abs(L);
    AssignmentValue required_value = (L > 0) ? TRUE : FALSE;

    if (solver->assignment[variable] != UNASSIGNED) {
        return (solver->assignment[variable] == required_value);
    }

    solver->assignment[variable] = required_value;
    solver->decision_level[variable] = solver->current_level;
    solver->reason[variable] = reason_clause;

    // Append L to solver.trail
    if (solver->trail_size >= solver->trail_capacity) {
        solver->trail_capacity = (solver->trail_capacity == 0) ? 16 : solver->trail_capacity * 2;
        solver->trail = (int*)realloc(solver->trail, solver->trail_capacity * sizeof(int));
    }
    solver->trail[solver->trail_size++] = L;

    return true;
}

#define NO_CONFLICT NULL

static inline AssignmentValue LIT_VALUE(const CDCLSolver* solver, int lit) {
    int var = abs(lit);
    AssignmentValue val = solver->assignment[var];
    if (val == UNASSIGNED) return UNASSIGNED;
    if (lit > 0) return val;
    return (val == TRUE) ? FALSE : TRUE;
}

Clause* PROPAGATE(CDCLSolver* solver) {
    size_t n = solver->number_of_variables;

    // WHILE solver.propagation_head < LENGTH(solver.trail):
    while (solver->propagation_head < solver->trail_size) {
        // assigned_literal = solver.trail[solver.propagation_head]
        // Increment solver.propagation_head.
        int assigned_literal = solver->trail[solver->propagation_head++];

        // false_literal = COMPLEMENT(assigned_literal)
        int false_literal = -assigned_literal;

        // watch_list = solver.watch_lists[INDEX(false_literal)]
        size_t false_idx = LIT_TO_INDEX(false_literal, n);
        Clause*** p_watch_list = &solver->watch_lists[false_idx];
        Clause** watch_list = *p_watch_list;
        int* p_watch_size = &solver->watch_list_sizes[false_idx];

        int read_index = 0;
        int write_index = 0;

        // WHILE read_index < LENGTH(watch_list):
        while (read_index < *p_watch_size) {
            // C = watch_list[read_index]
            // Increment read_index.
            Clause* C = watch_list[read_index++];

            // IF C is deleted:
            //     Continue.
            if (C == NULL || C->is_deleted) {
                continue;
            }

            // Ensure C.literals[0] is the false watched literal.
            if (C->literals[1] == false_literal) {
                int tmp = C->literals[0];
                C->literals[0] = C->literals[1];
                C->literals[1] = tmp;
            }

            if (C->literals[0] != false_literal) {
                // The list contains a stale or invalid watch entry.
                // Handle the invariant violation safely.
                continue;
            }

            int other_watch = C->literals[1];

            // Case 1: The other watch already satisfies the clause.
            if (LIT_VALUE(solver, other_watch) == TRUE) {
                watch_list[write_index++] = C;
                continue;
            }

            // Case 2: Find a replacement for the false watch.
            bool replacement_found = false;

            for (int position = 2; position < C->size; position++) {
                int candidate = C->literals[position];

                if (LIT_VALUE(solver, candidate) != FALSE) {
                    C->literals[0] = candidate;
                    C->literals[position] = false_literal;

                    // Append C to new_watch_list.
                    size_t cand_idx = LIT_TO_INDEX(candidate, n);
                    if (solver->watch_list_sizes[cand_idx] >= solver->watch_list_capacities[cand_idx]) {
                        solver->watch_list_capacities[cand_idx] *= 2;
                        solver->watch_lists[cand_idx] = (Clause**)realloc(
                            solver->watch_lists[cand_idx],
                            solver->watch_list_capacities[cand_idx] * sizeof(Clause*)
                        );
                    }
                    solver->watch_lists[cand_idx][solver->watch_list_sizes[cand_idx]++] = C;

                    replacement_found = true;
                    break;
                }
            }

            if (replacement_found) {
                // Do not copy C into the old watch list.
                continue;
            }

            // Case 3: No replacement exists.
            // The false watch must remain in the old list.
            watch_list[write_index++] = C;

            if (LIT_VALUE(solver, other_watch) == FALSE) {
                // Copy every unprocessed watch-list entry into the remaining positions.
                while (read_index < *p_watch_size) {
                    watch_list[write_index++] = watch_list[read_index++];
                }
                *p_watch_size = write_index;

                // Return C as the conflicting clause.
                return C;
            }

            // The other watch is unassigned.
            if (ENQUEUE(solver, other_watch, C) == false) {
                // Preserve all unprocessed watch-list entries.
                // Truncate the list to its valid entries.
                while (read_index < *p_watch_size) {
                    watch_list[write_index++] = watch_list[read_index++];
                }
                *p_watch_size = write_index;

                // Return C as the conflicting clause.
                return C;
            }
        }

        // Truncate watch_list to write_index.
        *p_watch_size = write_index;
    }

    return NO_CONFLICT;
}

#define ANALYSIS_ERROR -1
#define NO_POSITION -1

typedef struct {
    int* literals;
    int size;
    int capacity;
} IntList;

static inline void int_list_init(IntList* list, int cap) {
    list->size = 0;
    list->capacity = cap < 4 ? 4 : cap;
    list->literals = (int*)malloc(list->capacity * sizeof(int));
}

static inline void int_list_push(IntList* list, int val) {
    if (list->size >= list->capacity) {
        list->capacity *= 2;
        list->literals = (int*)realloc(list->literals, list->capacity * sizeof(int));
    }
    list->literals[list->size++] = val;
}

int ANALYZE_CONFLICT(CDCLSolver* solver, Clause* conflict_clause, Clause** out_learned_clause) {
    // Set solver.solver_mode to CONFLICT_ANALYSIS.
    solver->solver_mode = CONFLICT_ANALYSIS;

    // learned_clause = empty list
    // Append a placeholder for the asserting literal.
    IntList learned_clause;
    int_list_init(&learned_clause, 16);
    int_list_push(&learned_clause, 0); // Placeholder for index 0

    // Clear solver.seen.
    // Clear solver.bump_variables.
    for (int i = 0; i < solver->bump_variables_count; i++) {
        solver->seen[solver->bump_variables[i]] = false;
    }
    solver->bump_variables_count = 0;

    int path_count = 0;
    int trail_index = solver->trail_size - 1;
    Clause* current_clause = conflict_clause;

    // LOOP:
    while (true) {
        // FOR EACH literal L in current_clause:
        for (int i = 0; i < current_clause->size; i++) {
            int L = current_clause->literals[i];
            int variable = abs(L);

            if (solver->seen[variable]) {
                continue;
            }

            if (solver->decision_level[variable] == 0) {
                continue;
            }

            solver->seen[variable] = true;

            // Append variable to solver.bump_variables.
            if (solver->bump_variables_count >= solver->bump_variables_capacity) {
                solver->bump_variables_capacity *= 2;
                solver->bump_variables = (int*)realloc(solver->bump_variables,
                    solver->bump_variables_capacity * sizeof(int));
            }
            solver->bump_variables[solver->bump_variables_count++] = variable;

            if (solver->decision_level[variable] == solver->current_level) {
                path_count++;
            } else {
                int_list_push(&learned_clause, L);
            }
        }

        // Skip rule: find the most recent relevant assignment.
        while (trail_index >= 0 &&
               solver->seen[abs(solver->trail[trail_index])] == false) {
            trail_index--;
        }

        if (trail_index < 0) {
            // Report a conflict-analysis invariant failure.
            // Clear all temporary seen marks.
            // Return ANALYSIS_ERROR.
            for (int i = 0; i < solver->bump_variables_count; i++) {
                solver->seen[solver->bump_variables[i]] = false;
            }
            free(learned_clause.literals);
            *out_learned_clause = NULL;
            return ANALYSIS_ERROR;
        }

        int resolved_literal = solver->trail[trail_index];
        int variable = abs(resolved_literal);

        trail_index--;

        // This assignment has now been processed.
        solver->seen[variable] = false;
        path_count--;

        // The first UIP has been reached.
        if (path_count == 0) {
            int asserting_literal = -resolved_literal;
            learned_clause.literals[0] = asserting_literal;
            break;
        }

        Clause* reason_clause = solver->reason[variable];

        if (reason_clause == NO_REASON) {
            // Report a conflict-analysis invariant failure.
            // Clear all temporary seen marks.
            // Return ANALYSIS_ERROR.
            for (int i = 0; i < solver->bump_variables_count; i++) {
                solver->seen[solver->bump_variables[i]] = false;
            }
            free(learned_clause.literals);
            *out_learned_clause = NULL;
            return ANALYSIS_ERROR;
        }

        current_clause = reason_clause;
    }

    // Remove duplicate literals from learned_clause.
    // Validate that learned_clause is not tautological.
    int write_idx = 1;
    for (int i = 1; i < learned_clause.size; i++) {
        int lit = learned_clause.literals[i];
        bool duplicate = false;
        for (int j = 1; j < write_idx; j++) {
            if (learned_clause.literals[j] == lit) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            learned_clause.literals[write_idx++] = lit;
        }
    }
    learned_clause.size = write_idx;

    // Determine the non-chronological backtrack level.
    int backtrack_level = 0;
    int highest_level_position = NO_POSITION;

    for (int pos = 1; pos < learned_clause.size; pos++) {
        int L = learned_clause.literals[pos];
        int level = solver->decision_level[abs(L)];

        if (level > backtrack_level) {
            backtrack_level = level;
            highest_level_position = pos;
        }
    }

    if (highest_level_position != NO_POSITION) {
        int tmp = learned_clause.literals[1];
        learned_clause.literals[1] = learned_clause.literals[highest_level_position];
        learned_clause.literals[highest_level_position] = tmp;
    }

    // Clear all temporary seen marks.
    for (int i = 0; i < solver->bump_variables_count; i++) {
        solver->seen[solver->bump_variables[i]] = false;
    }

    // Allocate output learned Clause
    Clause* res_clause = (Clause*)malloc(sizeof(Clause));
    res_clause->size = learned_clause.size;
    res_clause->literals = learned_clause.literals;
    res_clause->is_learned = true;
    res_clause->activity = 0.0;
    res_clause->is_deleted = false;

    *out_learned_clause = res_clause;

    // Return backtrack_level
    return backtrack_level;
}

#define SUCCESS  0
#define ERROR   -1

int BACKTRACK(CDCLSolver* solver, int target_level) {
    // IF target_level > solver.current_level:
    //     Report an invalid backtrack target.
    //     Return ERROR.
    if (target_level > solver->current_level) {
        fprintf(stderr, "Error: Invalid backtrack target %d > current level %d\n",
                target_level, solver->current_level);
        return ERROR;
    }

    // WHILE solver.current_level > target_level:
    while (solver->current_level > target_level) {
        // IF solver.trail_limits is empty:
        //     Report an inconsistent decision-level state.
        //     Return ERROR.
        if (solver->trail_limits_size <= 0) {
            fprintf(stderr, "Error: Inconsistent decision-level state, trail_limits is empty\n");
            return ERROR;
        }

        // trail_start = Remove the last entry from solver.trail_limits.
        int trail_start = solver->trail_limits[--solver->trail_limits_size];

        // WHILE LENGTH(solver.trail) > trail_start:
        while (solver->trail_size > trail_start) {
            // L = Remove the last literal from solver.trail.
            int L = solver->trail[--solver->trail_size];
            int variable = abs(L);

            // Phase saving: save polarity before unassigning
            if (solver->saved_polarity) {
                solver->saved_polarity[variable] = solver->assignment[variable];
            }

            solver->assignment[variable] = UNASSIGNED;
            solver->decision_level[variable] = 0;
            solver->reason[variable] = NO_REASON;
        }

        // Decrement solver.current_level.
        solver->current_level--;
    }

    // Never move the propagation cursor past the remaining trail.
    if (solver->propagation_head > solver->trail_size) {
        solver->propagation_head = solver->trail_size;
    }

    return SUCCESS;
}

#include <math.h>
#include <limits.h>

#define MAX_RESTART_LIMIT (INT_MAX / 2)

int COMPUTE_RESTART_LIMIT(int base, int restart_index) {
    // limit = base * 1.5 raised to restart_index
    double calculated = (double)base * pow(1.5, (double)restart_index);

    // Return the integer limit, subject to a safe maximum.
    if (calculated >= (double)MAX_RESTART_LIMIT) {
        return MAX_RESTART_LIMIT;
    }

    return (int)calculated;
}

int RESTART(CDCLSolver* solver) {
    // Restart only after a completed conflict analysis and
    // learned-clause insertion, not in the middle of analysis.
    if (BACKTRACK(solver, 0) == ERROR) {
        return ERROR;
    }

    solver->conflicts_since_restart = 0;
    solver->restart_index++;

    solver->restart_limit = COMPUTE_RESTART_LIMIT(
        solver->restart_base,
        solver->restart_index
    );

    return SUCCESS;
}

#define CONTINUE 1
#define FAILURE -1

static inline bool IS_RESTART_DUE(const CDCLSolver* solver) {
    return solver->conflicts_since_restart >= solver->restart_limit;
}

bool IS_DATABASE_REFRESH_DUE(const CDCLSolver* solver) {
    // IF solver.conflict_count < solver.next_refresh_conflict:
    //     RETURN FALSE
    if (solver->conflict_count < solver->next_refresh_conflict) {
        return false;
    }

    // IF solver.learned_clause_count < solver.min_learned_clauses:
    //     RETURN FALSE
    if (solver->learned_clause_count < solver->min_learned_clauses) {
        return false;
    }

    // RETURN TRUE
    return true;
}

int HANDLE_CONFLICT(CDCLSolver* solver, Clause* conflict_clause) {
    // IF solver.current_level == 0:
    //     RETURN UNSAT
    if (solver->current_level == 0) {
        return UNSAT;
    }

    // (learned_clause, backtrack_level) = ANALYZE_CONFLICT(solver, conflict_clause)
    Clause* learned_clause = NULL;
    int backtrack_level = ANALYZE_CONFLICT(solver, conflict_clause, &learned_clause);

    // IF learned_clause is INVALID:
    //     RETURN ERROR
    if (backtrack_level == ANALYSIS_ERROR || learned_clause == NULL) {
        return ERROR;
    }

    // BUMP_VSIDS_SCORES(solver, learned_clause)
    // DECAY_VSIDS_ACTIVITY(solver)
    UPDATE_BRANCHING_HEURISTIC(solver);

    // learned_id = ADD_LEARNED_CLAUSE(solver, learned_clause)
    if (solver->clause_count >= solver->clause_capacity) {
        solver->clause_capacity = (solver->clause_capacity == 0) ? 16 : solver->clause_capacity * 2;
        solver->clauses = (Clause**)realloc(solver->clauses, solver->clause_capacity * sizeof(Clause*));
    }
    solver->clauses[solver->clause_count++] = learned_clause;
    solver->learned_clause_count++;

    // ATTACH_CLAUSE_TO_WATCHLISTS(solver, learned_id)
    ATTACH_CLAUSE(solver, learned_clause);

    // BACKTRACK(solver, backtrack_level)
    if (BACKTRACK(solver, backtrack_level) == ERROR) {
        return ERROR;
    }

    // asserting_literal = learned_clause[0]
    int asserting_literal = learned_clause->literals[0];

    // IF ENQUEUE(solver, asserting_literal, backtrack_level, learned_id) == FAILURE:
    //     RETURN ERROR
    if (!ENQUEUE(solver, asserting_literal, learned_clause)) {
        return ERROR;
    }

    // solver.conflict_count += 1
    solver->conflict_count += 1;
    solver->conflicts_since_restart += 1;

    // IF IS_DATABASE_REFRESH_DUE(solver):
    //     REFRESH_CLAUSE_DATABASE(solver)
    if (IS_DATABASE_REFRESH_DUE(solver)) {
        REFRESH_CLAUSE_DATABASE(solver);
    }

    // IF IS_RESTART_DUE(solver):
    //     RESTART(solver)
    if (IS_RESTART_DUE(solver)) {
        RESTART(solver);
    }

    // RETURN CONTINUE
    return CONTINUE;
}

#define UNDEFINED UNASSIGNED
#define VAL_TRUE  TRUE
#define VAL_FALSE FALSE

AssignmentValue CHOOSE_POLARITY(CDCLSolver* solver, int variable) {
    // IF solver.saved_polarity[variable] != UNDEFINED:
    //     RETURN solver.saved_polarity[variable]
    if (solver->saved_polarity && solver->saved_polarity[variable] != UNDEFINED) {
        return solver->saved_polarity[variable];
    }

    // RETURN VAL_TRUE
    return VAL_TRUE;
}

SolverMode SOLVE(CDCLSolver* solver, int number_of_variables, Clause** input_clauses, int input_clause_count) {
    // result = INITIALIZE_SOLVER(solver, number_of_variables, input_clauses)
    SolverMode result = INITIALIZE_SOLVER(
        solver,
        number_of_variables,
        input_clauses,
        input_clause_count
    );

    // IF result == UNSAT:
    //     Return UNSAT.
    if (result == UNSAT) {
        return UNSAT;
    }

    // IF result is not SUCCESS (or READY):
    //     Return ERROR.
    if (result != (SolverMode)SUCCESS && result != READY) {
        return (SolverMode)ERROR;
    }

    // WHILE TRUE:
    while (true) {
        Clause* conflict_clause = PROPAGATE(solver);

        if (conflict_clause != NO_CONFLICT) {
            // IF a conflict occurs:
            //     HANDLE_CONFLICT(solver, conflict_clause).
            int handle_res = HANDLE_CONFLICT(solver, conflict_clause);

            if (handle_res == UNSAT) {
                return UNSAT;
            }

            if (handle_res != CONTINUE && handle_res != SUCCESS) {
                return (SolverMode)ERROR;
            }

            continue;
        }

        // next_variable = VSIDS_SELECT_VARIABLE(solver.vsids, solver.assignment)
        int* assignment_ints = (int*)solver->assignment;
        size_t next_var_size_t = vsids_select_variable(&solver->vsids, assignment_ints);
        int next_variable = (next_var_size_t == NO_VARIABLE) ? 0 : (int)next_var_size_t;

        // IF next_variable does not exist:
        //     Set solver.solver_mode to SAT.
        //     Return SAT with the current assignment.
        if (next_variable == 0) {
            solver->solver_mode = SAT;
            return SAT;
        }

        AssignmentValue pol = CHOOSE_POLARITY(solver, next_variable);
        int decision_literal = (pol == VAL_TRUE) ? next_variable : -next_variable;

        // Create a new decision level.
        solver->current_level++;
        if (solver->trail_limits_size >= solver->trail_limits_capacity) {
            solver->trail_limits_capacity *= 2;
            solver->trail_limits = (int*)realloc(solver->trail_limits,
                solver->trail_limits_capacity * sizeof(int));
        }
        solver->trail_limits[solver->trail_limits_size++] = solver->trail_size;

        // Enqueue decision_literal with NO_REASON.
        // IF enqueue fails:
        //     Report an invariant violation.
        //     Return ERROR.
        if (!ENQUEUE(solver, decision_literal, NO_REASON)) {
            fprintf(stderr, "Invariant violation: Failed to enqueue decision literal %d\n", decision_literal);
            return (SolverMode)ERROR;
        }
    }
}

static inline void remove_clause_from_watchlist(CDCLSolver* solver, Clause* C, int lit) {
    size_t idx = LIT_TO_INDEX(lit, solver->number_of_variables);
    Clause** list = solver->watch_lists[idx];
    int size = solver->watch_list_sizes[idx];
    for (int i = 0; i < size; i++) {
        if (list[i] == C) {
            list[i] = list[size - 1];
            solver->watch_list_sizes[idx]--;
            break;
        }
    }
}

static inline bool is_clause_a_reason(const CDCLSolver* solver, const Clause* C) {
    for (int i = 1; i <= solver->number_of_variables; i++) {
        if (solver->assignment[i] != UNASSIGNED && solver->reason[i] == C) {
            return true;
        }
    }
    return false;
}

static int compare_clause_candidates(const void* a, const void* b) {
    Clause* ca = *(Clause**)a;
    Clause* cb = *(Clause**)b;

    // 1. Clause activity, from lowest to highest.
    if (ca->activity < cb->activity) return -1;
    if (ca->activity > cb->activity) return 1;

    // 3. Clause size, from largest to smallest, as a tie-breaker.
    if (ca->size > cb->size) return -1;
    if (ca->size < cb->size) return 1;

    return 0;
}

int REFRESH_CLAUSE_DATABASE(CDCLSolver* solver) {
    // candidates = empty list
    int candidate_cap = 64;
    Clause** candidates = (Clause**)malloc(candidate_cap * sizeof(Clause*));
    int candidate_count = 0;

    // FOR EACH clause C in solver.clauses:
    for (int i = 0; i < solver->clause_count; i++) {
        Clause* C = solver->clauses[i];

        // IF C is an original input clause:
        //     Continue.
        if (C == NULL || !C->is_learned) {
            continue;
        }

        // IF C is already deleted:
        //     Continue.
        if (C->is_deleted) {
            continue;
        }

        // IF C is a unit clause:
        //     Continue.
        if (C->size <= 1) {
            continue;
        }

        // IF C is currently used as the reason for any assigned variable:
        //     Continue.
        if (is_clause_a_reason(solver, C)) {
            continue;
        }

        // IF C is protected by the clause-retention policy:
        //     Continue.
        // (Default policy: keep highly active clauses or short clauses)

        // Add C to candidates.
        if (candidate_count >= candidate_cap) {
            candidate_cap *= 2;
            candidates = (Clause**)realloc(candidates, candidate_cap * sizeof(Clause*));
        }
        candidates[candidate_count++] = C;
    }

    // Rank candidates from least useful to most useful
    qsort(candidates, candidate_count, sizeof(Clause*), compare_clause_candidates);

    // Select the least useful candidates for deletion
    // until the configured database budget is satisfied (delete up to half of candidates)
    int to_delete_count = candidate_count / 2;

    for (int i = 0; i < to_delete_count; i++) {
        Clause* C = candidates[i];

        // Remove C from both of its watch lists.
        if (C->size >= 2) {
            remove_clause_from_watchlist(solver, C, C->literals[0]);
            remove_clause_from_watchlist(solver, C, C->literals[1]);
        }

        // Mark C as deleted.
        C->is_deleted = true;

        // Release its allocated storage safely.
        if (C->literals) {
            free(C->literals);
            C->literals = NULL;
        }
    }

    free(candidates);

    // Compact the clause database if needed: remove deleted clauses
    int write_idx = 0;
    for (int i = 0; i < solver->clause_count; i++) {
        Clause* C = solver->clauses[i];
        if (C != NULL && C->is_deleted) {
            free(C);
            solver->learned_clause_count--;
        } else {
            solver->clauses[write_idx++] = C;
        }
    }
    solver->clause_count = write_idx;

    // After refreshing the database:
    // solver.next_refresh_conflict = solver.conflict_count + solver.refresh_interval
    solver->next_refresh_conflict = solver->conflict_count + solver->refresh_interval;

    return SUCCESS;
}

int INITIALIZE_REASONING_SUBSYSTEMS(CDCLSolver* solver, int variables) {
    // INITIALIZE_MEMORY_MANAGER(solver.memory_manager)
    MemoryManager* mm_ptr = NULL;
    if (initialize_memory_manager(variables + 64, &mm_ptr) != STATUS_SUCCESS || !mm_ptr) {
        return FAILURE;
    }
    solver->memory_manager = *mm_ptr;
    free(mm_ptr);

    // IF VSIDS_INITIALIZE_WITH_MANAGER(solver.vsids, solver.memory_manager, variables) == FAILURE:
    //     RETURN FAILURE
    if (!vsids_initialize_with_manager(&solver->vsids, &solver->memory_manager, variables, DEFAULT_DECAY_FACTOR)) {
        return FAILURE;
    }

    // INITIALIZE_TRAIL(solver)
    solver->trail_capacity = variables > 0 ? variables : 16;
    solver->trail = (int*)malloc(solver->trail_capacity * sizeof(int));
    solver->trail_size = 0;

    // INITIALIZE_TRAIL_LIMITS(solver)
    solver->trail_limits_capacity = 32;
    solver->trail_limits = (int*)malloc(solver->trail_limits_capacity * sizeof(int));
    solver->trail_limits_size = 0;

    // INITIALIZE_WATCH_LISTS(solver, variables)
    int watch_list_total = 2 * variables + 1;
    solver->watch_lists = (Clause***)malloc(watch_list_total * sizeof(Clause**));
    solver->watch_list_sizes = (int*)calloc(watch_list_total, sizeof(int));
    solver->watch_list_capacities = (int*)malloc(watch_list_total * sizeof(int));
    for (int i = 0; i < watch_list_total; i++) {
        solver->watch_list_capacities[i] = 4;
        solver->watch_lists[i] = (Clause**)malloc(solver->watch_list_capacities[i] * sizeof(Clause*));
    }

    // INITIALIZE_CLAUSE_DATABASE(solver)
    solver->clause_capacity = 64;
    solver->clauses = (Clause**)malloc(solver->clause_capacity * sizeof(Clause*));
    solver->clause_count = 0;

    // INITIALIZE_SEEN_ARRAY(solver, variables)
    solver->seen = (bool*)calloc(variables + 1, sizeof(bool));

    // INITIALIZE_BUMP_VARIABLES(solver, variables)
    solver->bump_variables_capacity = 32;
    solver->bump_variables = (int*)malloc(solver->bump_variables_capacity * sizeof(int));
    solver->bump_variables_count = 0;

    // INITIALIZE_SAVED_POLARITY(solver, variables)
    solver->saved_polarity = (AssignmentValue*)malloc((variables + 1) * sizeof(AssignmentValue));
    for (int i = 1; i <= variables; i++) {
        solver->saved_polarity[i] = UNASSIGNED;
    }

    return SUCCESS;
}

void UPDATE_BRANCHING_HEURISTIC(CDCLSolver* solver) {
    // FOR EACH variable in solver.bump_variables:
    //     VSIDS_BUMP(solver.vsids, variable).
    for (int i = 0; i < solver->bump_variables_count; i++) {
        vsids_bump(&solver->vsids, (size_t)solver->bump_variables[i]);
    }

    // VSIDS_DECAY(solver.vsids).
    vsids_decay(&solver->vsids);
}

int SELECT_BRANCH_VARIABLE(CDCLSolver* solver) {
    // variable = VSIDS_SELECT_VARIABLE(solver.vsids, solver.assignment)
    size_t var = vsids_select_variable(&solver->vsids, (const int*)solver->assignment);

    // Return variable.
    return (var == NO_VARIABLE) ? 0 : (int)var;
}

void DESTROY_SOLVER(CDCLSolver* solver) {
    if (!solver) return;

    // Release the trail and decision-level arrays.
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

    // Release the propagation and watch-list storage.
    if (solver->watch_lists) {
        int watch_list_total = 2 * solver->number_of_variables + 1;
        for (int i = 0; i < watch_list_total; i++) {
            if (solver->watch_lists[i]) {
                free(solver->watch_lists[i]);
            }
        }
        free(solver->watch_lists);
        solver->watch_lists = NULL;
    }
    if (solver->watch_list_sizes) {
        free(solver->watch_list_sizes);
        solver->watch_list_sizes = NULL;
    }
    if (solver->watch_list_capacities) {
        free(solver->watch_list_capacities);
        solver->watch_list_capacities = NULL;
    }

    // Release the seen array and conflict-analysis scratch storage.
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

    // Release each clause exactly once according to its owner.
    // Learned clauses are owned and allocated by the solver.
    if (solver->clauses) {
        for (int i = 0; i < solver->clause_count; i++) {
            Clause* C = solver->clauses[i];
            if (C != NULL && C->is_learned) {
                if (C->literals) {
                    free(C->literals);
                    C->literals = NULL;
                }
                free(C);
                solver->clauses[i] = NULL;
            }
        }
        // Release the clause database.
        free(solver->clauses);
        solver->clauses = NULL;
    }
    solver->clause_count = 0;
    solver->clause_capacity = 0;

    // VSIDS_CLEANUP(solver.vsids).
    vsids_cleanup(&solver->vsids);

    // Destroy the MemoryManager only after every dependent
    // subsystem has finished using it.
    destroy_memory_manager(&solver->memory_manager);

    // Clear all pointers and reset the solver state.
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
