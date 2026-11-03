/**
 * test_harness.h
 * =============
 * Minimal single-header test framework for the Sodium CDCL test suite.
 *
 * Usage
 * -----
 *   #include "test_harness.h"
 *
 *   static void my_test(void) {
 *       TEST_ASSERT(1 + 1 == 2, "arithmetic broken");
 *   }
 *
 *   int main(void) {
 *       RUN_TEST(my_test);
 *       REPORT_AND_EXIT();
 *   }
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../CDCL/CDCL.h"   /* CDCLSolver, Clause, INITIALIZE_SOLVER, … */

#ifndef ANALYSIS_ERROR
#define ANALYSIS_ERROR -1
#endif

/* ── Global counters (one set per binary, all statics are local to the TU) ── */
static int th_pass_count    = 0;
static int th_fail_count    = 0;
static int th_assert_failed = 0; /* Set inside a test to signal failure      */

/* ── Core macros ──────────────────────────────────────────────────────────── */

/**
 * TEST_ASSERT – evaluate a condition; if false, print location + message
 * and mark the current test as failed.  Does NOT abort; every assertion in
 * the test still runs so the full failure set is reported.
 */
#define TEST_ASSERT(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "  ASSERT FAILED [%s:%d]: %s\n",                  \
                    __FILE__, __LINE__, (msg));                                \
            th_assert_failed = 1;                                              \
        }                                                                      \
    } while (0)

/**
 * RUN_TEST – call fn(), then emit [PASS] or [FAIL] based on whether any
 * TEST_ASSERT fired inside fn().
 */
#define RUN_TEST(fn)                                                           \
    do {                                                                       \
        th_assert_failed = 0;                                                  \
        (fn)();                                                                \
        if (th_assert_failed) {                                                \
            fprintf(stderr, "[FAIL] %s\n", #fn);                              \
            th_fail_count++;                                                   \
        } else {                                                               \
            printf("[PASS] %s\n", #fn);                                       \
            th_pass_count++;                                                   \
        }                                                                      \
    } while (0)

/**
 * REPORT_AND_EXIT – print the final pass/fail summary and exit with a
 * non-zero status code if any test failed.  Place at the end of main().
 */
#define REPORT_AND_EXIT()                                                      \
    do {                                                                       \
        printf("\nResults: %d passed, %d failed\n",                           \
               th_pass_count, th_fail_count);                                 \
        return (th_fail_count > 0) ? 1 : 0;                                   \
    } while (0)

/* ── Clause helpers ───────────────────────────────────────────────────────── */

/**
 * make_clause – heap-allocate a Clause whose literals are copied from lits[].
 *
 * Ownership rules
 * ───────────────
 * • is_learned == false  → caller owns the clause; free with free_clause().
 * • is_learned == true   → DESTROY_SOLVER takes ownership; do NOT free with
 *                          free_clause() after handing to the solver.
 */
static inline Clause* make_clause(const int* lits, int n, bool is_learned) {
    Clause* c = (Clause*)malloc(sizeof(Clause));
    if (!c) { perror("malloc Clause"); abort(); }
    c->size       = n;
    c->is_learned = is_learned;
    c->activity   = 0.0;
    c->is_deleted = false;
    if (n > 0 && lits) {
        c->literals = (int*)malloc((size_t)n * sizeof(int));
        if (!c->literals) { perror("malloc literals"); abort(); }
        memcpy(c->literals, lits, (size_t)n * sizeof(int));
    } else {
        c->literals = NULL;
    }
    return c;
}

/**
 * free_clause – release a NON-learned clause and its literals array.
 * Never call this on a learned clause that the solver already owns.
 */
static inline void free_clause(Clause* c) {
    if (!c) return;
    free(c->literals);
    free(c);
}

/**
 * make_solver – initialise a CDCLSolver via INITIALIZE_SOLVER.
 *
 * Returns 0 on success.  On failure (INITIALIZE_SOLVER returned UNSAT or an
 * error code), returns -1.  The solver memory is already cleaned up by
 * INITIALIZE_SOLVER on failure, so the caller must NOT call DESTROY_SOLVER.
 *
 * On success the caller MUST call DESTROY_SOLVER when done.
 */
static inline int make_solver(CDCLSolver* solver,
                               int n_vars,
                               Clause** clauses,
                               int n_clauses) {
    SolverMode r = INITIALIZE_SOLVER(solver, n_vars, clauses, n_clauses);
    /* SUCCESS == 0, cast to SolverMode gives NORMAL_SEARCH (also 0).        */
    return (r == (SolverMode)SUCCESS) ? 0 : -1;
}

/**
 * check_assignment_satisfies_clause – returns true if at least one literal in
 * clause C evaluates to TRUE under the current solver assignment.
 */
static inline bool check_clause_satisfied(const CDCLSolver* solver,
                                           const Clause* C) {
    for (int i = 0; i < C->size; i++) {
        int lit = C->literals[i];
        int var = abs(lit);
        if (var < 1 || var > solver->number_of_variables) continue;
        AssignmentValue val = solver->assignment[var];
        if (val == UNASSIGNED) continue;
        if (lit > 0 && val == TRUE)  return true;
        if (lit < 0 && val == FALSE) return true;
    }
    return false;
}

#endif /* TEST_HARNESS_H */
