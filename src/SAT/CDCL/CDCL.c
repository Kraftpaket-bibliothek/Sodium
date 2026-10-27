//This will be the execution of the CDCL engine.
#include "CDCL.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

/* Vector utility functions */
static void lit_vector_push(LitVector* v, int val) {
    if (v->size == v->capacity) {
        v->capacity = v->capacity == 0 ? 8 : v->capacity * 2;
        v->data = (int*)realloc(v->data, v->capacity * sizeof(int));
    }
    v->data[v->size++] = val;
}

static void size_vector_push(SizeVector* v, size_t val) {
    if (v->size == v->capacity) {
        v->capacity = v->capacity == 0 ? 8 : v->capacity * 2;
        v->data = (size_t*)realloc(v->data, v->capacity * sizeof(size_t));
    }
    v->data[v->size++] = val;
}

static void clause_vector_push(ClauseVector* v, Clause* val) {
    if (v->size == v->capacity) {
        v->capacity = v->capacity == 0 ? 8 : v->capacity * 2;
        v->data = (Clause**)realloc(v->data, v->capacity * sizeof(Clause*));
    }
    v->data[v->size++] = val;
}

/* ============================================================================
 * MODULE: TRAIL_MANAGER & SOLVER STATE
 * ============================================================================ */

void initialize_trail(CDCLSolver* solver, size_t number_of_variables) {
    solver->number_of_variables = number_of_variables;

    initialize_memory_manager(number_of_variables * 2 + 32, &solver->memory_manager);

    solver->assignment = (int*)calloc(number_of_variables + 1, sizeof(int));
    solver->decision_level = (size_t*)calloc(number_of_variables + 1, sizeof(size_t));
    solver->reason = (Clause**)calloc(number_of_variables + 1, sizeof(Clause*));

    solver->trail.data = NULL;
    solver->trail.size = 0;
    solver->trail.capacity = 0;

    solver->trail_limits.data = NULL;
    solver->trail_limits.size = 0;
    solver->trail_limits.capacity = 0;

    solver->current_level = 0;
    solver->propagation_head = 0;

    /* Allocate Watch Lists (indexed by LIT_TO_INDEX) */
    size_t total_literal_keys = 2 * number_of_variables + 1;
    solver->watch_lists = (ClauseVector*)calloc(total_literal_keys, sizeof(ClauseVector));

    solver->clauses.data = NULL;
    solver->clauses.size = 0;
    solver->clauses.capacity = 0;

    solver->seen = (bool*)calloc(number_of_variables + 1, sizeof(bool));
    solver->bump_variables.data = NULL;
    solver->bump_variables.size = 0;
    solver->bump_variables.capacity = 0;
}

static inline int lit_value(const CDCLSolver* solver, int literal) {
    int var = VARIABLE_OF(literal);
    int var_val = solver->assignment[var];
    if (var_val == UNASSIGNED) return UNASSIGNED;
    return (literal > 0) ? var_val : -var_val;
}

void new_decision_level(CDCLSolver* solver) {
    size_vector_push(&solver->trail_limits, solver->trail.size);
    solver->current_level++;
}

bool enqueue(CDCLSolver* solver, int literal, Clause* reason_clause) {
    size_t var = (size_t)VARIABLE_OF(literal);
    int required_value = (literal > 0) ? VAL_TRUE : VAL_FALSE;

    if (solver->assignment[var] != UNASSIGNED) {
        return solver->assignment[var] == required_value;
    }

    solver->assignment[var] = required_value;
    solver->decision_level[var] = solver->current_level;
    solver->reason[var] = reason_clause;
    lit_vector_push(&solver->trail, literal);

    return true;
}

void backtrack(CDCLSolver* solver, size_t target_level) {
    while (solver->current_level > target_level) {
        size_t trail_start = solver->trail_limits.data[--solver->trail_limits.size];

        while (solver->trail.size > trail_start) {
            int literal = solver->trail.data[--solver->trail.size];
            size_t var = (size_t)VARIABLE_OF(literal);

            solver->assignment[var] = UNASSIGNED;
            solver->decision_level[var] = 0;
            solver->reason[var] = NO_REASON;
        }

        solver->current_level--;
    }

    if (solver->propagation_head > solver->trail.size) {
        solver->propagation_head = solver->trail.size;
    }
}

/* ============================================================================
 * MODULE: BOOLEAN_CONSTRAINT_PROPAGATION (2-Watched Literals)
 * ============================================================================ */

Clause* propagate(CDCLSolver* solver) {
    while (solver->propagation_head < solver->trail.size) {
        int assigned_literal = solver->trail.data[solver->propagation_head++];
        int false_literal = NEGATE(assigned_literal);

        size_t false_idx = LIT_TO_INDEX(false_literal, solver->number_of_variables);
        ClauseVector* ws = &solver->watch_lists[false_idx];

        size_t i = 0;
        size_t j = 0;

        while (i < ws->size) {
            Clause* clause = ws->data[i];

            /* Normalize: ensure false_literal is at clause->lits[0] */
            if (clause->lits[1] == false_literal) {
                clause->lits[1] = clause->lits[0];
                clause->lits[0] = false_literal;
            }

            int other_watch = clause->lits[1];

            /* If other watch is TRUE, clause is already satisfied */
            if (lit_value(solver, other_watch) == VAL_TRUE) {
                ws->data[j++] = ws->data[i++];
                continue;
            }

            /* Look for replacement literal that is not FALSE */
            bool found_replacement = false;
            for (size_t k = 2; k < clause->size; k++) {
                if (lit_value(solver, clause->lits[k]) != VAL_FALSE) {
                    found_replacement = true;
                    clause->lits[0] = clause->lits[k];
                    clause->lits[k] = false_literal;

                    /* Move watch: register clause to new watched literal */
                    size_t new_idx = LIT_TO_INDEX(clause->lits[0], solver->number_of_variables);
                    clause_vector_push(&solver->watch_lists[new_idx], clause);
                    break;
                }
            }

            if (found_replacement) {
                /* Clause unlinked from ws[false_idx] by advancing i without writing to j */
                i++;
                continue;
            }

            /* No replacement found: keep watcher here */
            ws->data[j++] = ws->data[i++];

            /* Clause is either Conflict or Unit */
            if (lit_value(solver, other_watch) == VAL_FALSE) {
                /* Copy remaining watches so none are dropped */
                while (i < ws->size) {
                    ws->data[j++] = ws->data[i++];
                }
                ws->size = j;
                return clause; /* CONFLICT */
            }

            if (!enqueue(solver, other_watch, clause)) {
                while (i < ws->size) {
                    ws->data[j++] = ws->data[i++];
                }
                ws->size = j;
                return clause; /* CONFLICT */
            }
        }
        ws->size = j;
    }

    return NULL; /* NO_CONFLICT */
}

/* ============================================================================
 * MODULE: CONFLICT_ANALYSIS (1st Unique Implication Point)
 * ============================================================================ */

void analyze(CDCLSolver* solver, Clause* conflict_clause, LitVector* learned_clause, size_t* backtrack_level) {
    learned_clause->size = 0;
    lit_vector_push(learned_clause, 0); /* Reserve index 0 for asserting_literal */

    solver->bump_variables.size = 0;
    size_t path_count = 0;
    size_t trail_index = solver->trail.size - 1;
    Clause* clause_to_process = conflict_clause;
    size_t pivot_variable = NO_VARIABLE;
    int asserting_literal = 0;

    while (true) {
        for (size_t i = 0; i < clause_to_process->size; i++) {
            int lit = clause_to_process->lits[i];
            size_t var = (size_t)VARIABLE_OF(lit);

            if (var == pivot_variable) continue;
            if (solver->seen[var]) continue;
            if (solver->decision_level[var] == 0) continue;

            solver->seen[var] = true;
            size_vector_push(&solver->bump_variables, var);

            if (solver->decision_level[var] == solver->current_level) {
                path_count++;
            } else {
                lit_vector_push(learned_clause, lit);
            }
        }

        while (!solver->seen[VARIABLE_OF(solver->trail.data[trail_index])]) {
            trail_index--;
        }

        int resolved_literal = solver->trail.data[trail_index];
        trail_index--;

        pivot_variable = (size_t)VARIABLE_OF(resolved_literal);
        solver->seen[pivot_variable] = false;
        path_count--;

        if (path_count == 0) {
            asserting_literal = NEGATE(resolved_literal);
            break;
        }

        clause_to_process = solver->reason[pivot_variable];
    }

    /* Place asserting literal at index 0 */
    learned_clause->data[0] = asserting_literal;

    /* Compute backtrack level and swap highest decision level literal into index 1 */
    *backtrack_level = 0;
    size_t max_level_idx = 1;

    for (size_t i = 1; i < learned_clause->size; i++) {
        size_t var = (size_t)VARIABLE_OF(learned_clause->data[i]);
        size_t level = solver->decision_level[var];
        if (level > *backtrack_level) {
            *backtrack_level = level;
            max_level_idx = i;
        }
    }

    if (learned_clause->size > 1) {
        int temp = learned_clause->data[1];
        learned_clause->data[1] = learned_clause->data[max_level_idx];
        learned_clause->data[max_level_idx] = temp;
    }

    /* Clear seen array */
    for (size_t i = 0; i < solver->bump_variables.size; i++) {
        solver->seen[solver->bump_variables.data[i]] = false;
    }
}

/* ============================================================================
 * MODULE: CDCL_SOLVER
 * ============================================================================ */

Clause* create_clause(const int* lits, size_t size, bool is_learned) {
    Clause* c = (Clause*)malloc(sizeof(Clause));
    c->size = size;
    c->is_learned = is_learned;
    c->lits = (int*)malloc(size * sizeof(int));
    for (size_t i = 0; i < size; i++) {
        c->lits[i] = lits[i];
    }
    return c;
}

static void attach_clause_watches(CDCLSolver* solver, Clause* clause) {
    if (clause->size >= 2) {
        size_t w0 = LIT_TO_INDEX(clause->lits[0], solver->number_of_variables);
        size_t w1 = LIT_TO_INDEX(clause->lits[1], solver->number_of_variables);
        clause_vector_push(&solver->watch_lists[w0], clause);
        clause_vector_push(&solver->watch_lists[w1], clause);
    }
}

int select_polarity(size_t variable) {
    (void)variable;
    return VAL_FALSE; /* Default initial polarity */
}

bool solve(CDCLSolver* solver, size_t num_vars, Clause** original_clauses, size_t num_clauses) {
    initialize_trail(solver, num_vars);
    vsids_initialize_with_manager(&solver->vsids, solver->memory_manager, num_vars, DEFAULT_DECAY_FACTOR);

    /* Load initial clauses and handle unit clauses */
    for (size_t i = 0; i < num_clauses; i++) {
        Clause* c = original_clauses[i];
        clause_vector_push(&solver->clauses, c);

        if (c->size == 1) {
            if (!enqueue(solver, c->lits[0], c)) {
                return false; /* UNSATISFIABLE during initial unit setup */
            }
        } else {
            attach_clause_watches(solver, c);
        }
    }

    LitVector learned_clause;
    learned_clause.data = NULL;
    learned_clause.size = 0;
    learned_clause.capacity = 0;

    while (true) {
        Clause* conflict = propagate(solver);

        if (conflict != NULL) {
            if (solver->current_level == 0) {
                free(learned_clause.data);
                return false; /* UNSATISFIABLE */
            }

            size_t backtrack_level = 0;
            analyze(solver, conflict, &learned_clause, &backtrack_level);

            /* Bump variables involved in conflict */
            for (size_t i = 0; i < solver->bump_variables.size; i++) {
                vsids_bump(&solver->vsids, solver->bump_variables.data[i]);
            }
            vsids_decay(&solver->vsids);

            /* Create and register learned clause */
            Clause* learned = create_clause(learned_clause.data, learned_clause.size, true);
            clause_vector_push(&solver->clauses, learned);
            attach_clause_watches(solver, learned);

            /* Backtrack to calculated 1-UIP assertion level */
            backtrack(solver, backtrack_level);

            /* Enqueue asserting literal */
            int asserting_literal = learned_clause.data[0];
            if (!enqueue(solver, asserting_literal, learned)) {
                free(learned_clause.data);
                return false; /* UNSATISFIABLE */
            }

            continue;
        }

        /* Pick next decision branch */
        size_t next_var = vsids_select_variable(&solver->vsids, solver->assignment);
        if (next_var == NO_VARIABLE) {
            free(learned_clause.data);
            return true; /* SATISFIABLE */
        }

        int polarity = select_polarity(next_var);
        int decision_lit = (polarity == VAL_TRUE) ? (int)next_var : -(int)next_var;

        new_decision_level(solver);
        enqueue(solver, decision_lit, NO_REASON);
    }
}

/* ============================================================================
 * CLEANUP & DEMONSTRATION MAIN
 * ============================================================================ */

void destroy_solver(CDCLSolver* solver) {
    free(solver->assignment);
    free(solver->decision_level);
    free(solver->reason);
    free(solver->trail.data);
    free(solver->trail_limits.data);

    size_t total_keys = 2 * solver->number_of_variables + 1;
    for (size_t i = 0; i < total_keys; i++) {
        free(solver->watch_lists[i].data);
    }
    free(solver->watch_lists);

    for (size_t i = 0; i < solver->clauses.size; i++) {
        free(solver->clauses.data[i]->lits);
        free(solver->clauses.data[i]);
    }
    free(solver->clauses.data);

    vsids_cleanup(&solver->vsids);
    if (solver->memory_manager) {
        destroy_memory_manager(solver->memory_manager);
        solver->memory_manager = NULL;
    }
    free(solver->seen);
    free(solver->bump_variables.data);
}
