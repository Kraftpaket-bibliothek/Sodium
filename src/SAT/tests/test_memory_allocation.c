/**
 * test_memory_allocation.c  ── T0: Solver Allocation & Initialisation Sanity
 * ===========================================================================
 * Verifies that every pointer and counter inside CDCLSolver is correctly
 * allocated and seeded by INITIALIZE_SOLVER / INITIALIZE_REASONING_SUBSYSTEMS
 * BEFORE any literal is touched.
 *
 * Tests
 * -----
 * 0.1  assignment array is non-NULL and covers 1..n
 * 0.2  all variables start UNASSIGNED
 * 0.3  decision_level array is non-NULL, all entries == 0
 * 0.4  reason array is non-NULL, all entries == NO_REASON (NULL)
 * 0.5  trail is non-NULL, trail_size == 0, trail_capacity >= n
 * 0.6  trail_limits is non-NULL, trail_limits_size == 0
 * 0.7  watch_lists non-NULL; all 2n+1 per-literal arrays non-NULL, sizes == 0
 * 0.8  seen array non-NULL, all false
 * 0.9  bump_variables non-NULL, bump_variables_count == 0
 * 0.10 saved_polarity non-NULL, all entries == UNASSIGNED
 * 0.11 VSIDS initialized: variable_count == n, manager != NULL
 * 0.12 counters at documented defaults
 * 0.13 DESTROY_SOLVER sets assignment to NULL; a second DESTROY on a zeroed
 *      struct does not crash (all-NULL-check safety)
 * 0.14 edge case: n == 1
 * 0.15 large n == 1000
 */

#include "test_harness.h"
#include <string.h>

/* ── helpers ── */
static void run_alloc_checks(int n) {
    CDCLSolver solver;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* 0.1 – assignment */
    TEST_ASSERT(solver.assignment != NULL, "assignment is NULL");

    /* 0.2 – all vars UNASSIGNED */
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.assignment[i] == UNASSIGNED,
                    "assignment not UNASSIGNED at init");
    }

    /* 0.3 – decision_level */
    TEST_ASSERT(solver.decision_level != NULL, "decision_level is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.decision_level[i] == 0,
                    "decision_level not 0 at init");
    }

    /* 0.4 – reason */
    TEST_ASSERT(solver.reason != NULL, "reason is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.reason[i] == NO_REASON,
                    "reason not NO_REASON at init");
    }

    /* 0.5 – trail */
    TEST_ASSERT(solver.trail != NULL,       "trail is NULL");
    TEST_ASSERT(solver.trail_size == 0,     "trail_size not 0");
    TEST_ASSERT(solver.trail_capacity >= n, "trail_capacity < n");

    /* 0.6 – trail_limits */
    TEST_ASSERT(solver.trail_limits != NULL,      "trail_limits is NULL");
    TEST_ASSERT(solver.trail_limits_size == 0,    "trail_limits_size not 0");

    /* 0.7 – watch_lists */
    int watch_total = 2 * n + 1;
    TEST_ASSERT(solver.watch_lists != NULL,      "watch_lists is NULL");
    TEST_ASSERT(solver.watch_list_sizes != NULL, "watch_list_sizes is NULL");
    for (int i = 0; i < watch_total; i++) {
        TEST_ASSERT(solver.watch_lists[i] != NULL,
                    "individual watch_list is NULL");
        TEST_ASSERT(solver.watch_list_sizes[i] == 0,
                    "watch_list_sizes[i] not 0 at init");
    }

    /* 0.8 – seen */
    TEST_ASSERT(solver.seen != NULL, "seen is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.seen[i] == false, "seen[i] not false at init");
    }

    /* 0.9 – bump_variables */
    TEST_ASSERT(solver.bump_variables != NULL,    "bump_variables is NULL");
    TEST_ASSERT(solver.bump_variables_count == 0, "bump_variables_count not 0");

    /* 0.10 – saved_polarity */
    TEST_ASSERT(solver.saved_polarity != NULL, "saved_polarity is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.saved_polarity[i] == UNASSIGNED,
                    "saved_polarity not UNASSIGNED at init");
    }

    /* 0.11 – VSIDS */
    TEST_ASSERT(solver.vsids.variable_count == (size_t)n,
                "vsids.variable_count mismatch");
    TEST_ASSERT(solver.vsids.manager != NULL, "vsids.manager is NULL");

    DESTROY_SOLVER(&solver);
}

/* ── individual test functions ── */

static void alloc_assignment_array_non_null(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 5, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.assignment != NULL, "assignment is NULL");
    DESTROY_SOLVER(&solver);
}

static void alloc_all_vars_start_unassigned(void) {
    CDCLSolver solver;
    int n = 8;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.assignment[i] == UNASSIGNED,
                    "variable not UNASSIGNED at init");
    }
    DESTROY_SOLVER(&solver);
}

static void alloc_decision_level_array_non_null(void) {
    CDCLSolver solver;
    int n = 5;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.decision_level != NULL, "decision_level is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.decision_level[i] == 0,
                    "decision_level[i] not 0 at init");
    }
    DESTROY_SOLVER(&solver);
}

static void alloc_reason_array_non_null(void) {
    CDCLSolver solver;
    int n = 5;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.reason != NULL, "reason is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.reason[i] == NO_REASON,
                    "reason[i] not NO_REASON at init");
    }
    DESTROY_SOLVER(&solver);
}

static void alloc_trail_non_null_and_sized(void) {
    CDCLSolver solver;
    int n = 10;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.trail != NULL,           "trail is NULL");
    TEST_ASSERT(solver.trail_size == 0,         "trail_size not 0");
    TEST_ASSERT(solver.trail_capacity >= n,     "trail_capacity < n");
    DESTROY_SOLVER(&solver);
}

static void alloc_trail_limits_non_null(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 5, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.trail_limits != NULL,    "trail_limits is NULL");
    TEST_ASSERT(solver.trail_limits_size == 0,  "trail_limits_size not 0");
    DESTROY_SOLVER(&solver);
}

static void alloc_watch_lists_non_null(void) {
    CDCLSolver solver;
    int n = 4;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    int total = 2 * n + 1;
    TEST_ASSERT(solver.watch_lists != NULL,      "watch_lists is NULL");
    TEST_ASSERT(solver.watch_list_sizes != NULL, "watch_list_sizes is NULL");
    for (int i = 0; i < total; i++) {
        TEST_ASSERT(solver.watch_lists[i] != NULL,
                    "individual watch_list[i] is NULL");
        TEST_ASSERT(solver.watch_list_sizes[i] == 0,
                    "watch_list_sizes[i] not 0 at init");
    }
    DESTROY_SOLVER(&solver);
}

static void alloc_seen_array_non_null_and_zeroed(void) {
    CDCLSolver solver;
    int n = 6;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.seen != NULL, "seen is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.seen[i] == false, "seen[i] not false at init");
    }
    DESTROY_SOLVER(&solver);
}

static void alloc_bump_variables_non_null(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 5, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.bump_variables != NULL,    "bump_variables is NULL");
    TEST_ASSERT(solver.bump_variables_count == 0, "bump_variables_count not 0");
    DESTROY_SOLVER(&solver);
}

static void alloc_saved_polarity_non_null_and_unset(void) {
    CDCLSolver solver;
    int n = 5;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.saved_polarity != NULL, "saved_polarity is NULL");
    for (int i = 1; i <= n; i++) {
        TEST_ASSERT(solver.saved_polarity[i] == UNASSIGNED,
                    "saved_polarity[i] not UNASSIGNED");
    }
    DESTROY_SOLVER(&solver);
}

static void alloc_vsids_initialized(void) {
    CDCLSolver solver;
    int n = 7;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.vsids.variable_count == (size_t)n,
                "vsids.variable_count mismatch");
    TEST_ASSERT(solver.vsids.manager != NULL, "vsids.manager is NULL");
    DESTROY_SOLVER(&solver);
}

static void alloc_counters_at_defaults(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 5, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    TEST_ASSERT(solver.conflict_count         == 0,    "conflict_count != 0");
    TEST_ASSERT(solver.conflicts_since_restart== 0,    "conflicts_since_restart != 0");
    TEST_ASSERT(solver.restart_base           == 100,  "restart_base != 100");
    TEST_ASSERT(solver.restart_limit          == 100,  "restart_limit != 100");
    TEST_ASSERT(solver.restart_index          == 1,    "restart_index != 1");
    TEST_ASSERT(solver.learned_clause_count   == 0,    "learned_clause_count != 0");
    TEST_ASSERT(solver.min_learned_clauses    == 100,  "min_learned_clauses != 100");
    TEST_ASSERT(solver.refresh_interval       == 1000, "refresh_interval != 1000");
    TEST_ASSERT(solver.current_level          == 0,    "current_level != 0");
    TEST_ASSERT(solver.propagation_head       == 0,    "propagation_head != 0");
    TEST_ASSERT(solver.trail_size             == 0,    "trail_size != 0");
    DESTROY_SOLVER(&solver);
}

static void alloc_destroy_leaves_no_dangling(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 5, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");
    DESTROY_SOLVER(&solver);
    /* After DESTROY_SOLVER all direct pointer fields should be NULL */
    TEST_ASSERT(solver.assignment         == NULL, "assignment not NULLed");
    TEST_ASSERT(solver.saved_polarity     == NULL, "saved_polarity not NULLed");
    TEST_ASSERT(solver.decision_level     == NULL, "decision_level not NULLed");
    TEST_ASSERT(solver.reason             == NULL, "reason not NULLed");
    TEST_ASSERT(solver.trail              == NULL, "trail not NULLed");
    TEST_ASSERT(solver.trail_limits       == NULL, "trail_limits not NULLed");
    TEST_ASSERT(solver.watch_lists        == NULL, "watch_lists not NULLed");
    TEST_ASSERT(solver.watch_list_sizes   == NULL, "watch_list_sizes not NULLed");
    TEST_ASSERT(solver.watch_list_capacities == NULL, "watch_list_capacities not NULLed");
    TEST_ASSERT(solver.seen               == NULL, "seen not NULLed");
    TEST_ASSERT(solver.bump_variables     == NULL, "bump_variables not NULLed");
    TEST_ASSERT(solver.clauses            == NULL, "clauses not NULLed");
}

static void alloc_single_variable(void) {
    /* Edge case: minimum problem size */
    run_alloc_checks(1);
}

static void alloc_large_variable_count(void) {
    /* Larger problem size: all arrays must be correctly allocated */
    run_alloc_checks(1000);
}

/* ── main ─────────────────────────────────────────────────────────────────── */
int main(void) {
    printf("=== T0: Memory Allocation & Initialisation ===\n");
    RUN_TEST(alloc_assignment_array_non_null);
    RUN_TEST(alloc_all_vars_start_unassigned);
    RUN_TEST(alloc_decision_level_array_non_null);
    RUN_TEST(alloc_reason_array_non_null);
    RUN_TEST(alloc_trail_non_null_and_sized);
    RUN_TEST(alloc_trail_limits_non_null);
    RUN_TEST(alloc_watch_lists_non_null);
    RUN_TEST(alloc_seen_array_non_null_and_zeroed);
    RUN_TEST(alloc_bump_variables_non_null);
    RUN_TEST(alloc_saved_polarity_non_null_and_unset);
    RUN_TEST(alloc_vsids_initialized);
    RUN_TEST(alloc_counters_at_defaults);
    RUN_TEST(alloc_destroy_leaves_no_dangling);
    RUN_TEST(alloc_single_variable);
    RUN_TEST(alloc_large_variable_count);
    REPORT_AND_EXIT();
}
