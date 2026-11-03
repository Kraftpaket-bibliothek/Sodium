/**
 * test_enqueue_trail.c  ── T1: ENQUEUE + Trail Integrity
 * =======================================================
 * Tests the ENQUEUE function in isolation: correct assignment recording,
 * trail appending, conflict detection, and dynamic trail resizing.
 *
 * Tests
 * -----
 * 1.1  Enqueueing a positive literal sets assignment == TRUE
 * 1.2  Enqueueing a negative literal sets assignment == FALSE
 * 1.3  Re-enqueueing a consistent literal returns true, trail unchanged
 * 1.4  Enqueueing a contradicting literal returns false, trail unchanged
 * 1.5  reason pointer is stored correctly
 * 1.6  decision_level is stamped with solver.current_level at enqueue time
 * 1.7  Trail buffer reallocates correctly when capacity is artificially low
 */

#include "test_harness.h"

/* ── 1.1 ── */
static void enqueue_positive_literal(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    bool ok = ENQUEUE(&solver, 1, NO_REASON);

    TEST_ASSERT(ok == true,                    "ENQUEUE should return true");
    TEST_ASSERT(solver.assignment[1] == TRUE,  "assignment[1] should be TRUE");
    TEST_ASSERT(solver.trail_size    == 1,     "trail_size should be 1");
    TEST_ASSERT(solver.trail[0]      == 1,     "trail[0] should be 1");
    TEST_ASSERT(solver.reason[1]     == NULL,  "reason should be NULL");

    DESTROY_SOLVER(&solver);
}

/* ── 1.2 ── */
static void enqueue_negative_literal(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    bool ok = ENQUEUE(&solver, -2, NO_REASON);

    TEST_ASSERT(ok == true,                    "ENQUEUE should return true");
    TEST_ASSERT(solver.assignment[2] == FALSE, "assignment[2] should be FALSE");
    TEST_ASSERT(solver.trail_size    == 1,     "trail_size should be 1");
    TEST_ASSERT(solver.trail[0]      == -2,    "trail[0] should be -2");

    DESTROY_SOLVER(&solver);
}

/* ── 1.3 ── */
static void enqueue_already_consistent(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* First enqueue sets var 1 = TRUE */
    ENQUEUE(&solver, 1, NO_REASON);
    int size_before = solver.trail_size;

    /* Second enqueue of same literal must return true and not grow trail */
    bool ok = ENQUEUE(&solver, 1, NO_REASON);

    TEST_ASSERT(ok == true,                     "consistent re-enqueue should return true");
    TEST_ASSERT(solver.trail_size == size_before, "trail must not grow on consistent re-enqueue");

    DESTROY_SOLVER(&solver);
}

/* ── 1.4 ── */
static void enqueue_contradiction(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Assign var 1 = TRUE */
    ENQUEUE(&solver, 1, NO_REASON);
    int size_before = solver.trail_size;

    /* Attempting to assign var 1 = FALSE must return false */
    bool ok = ENQUEUE(&solver, -1, NO_REASON);

    TEST_ASSERT(ok == false,                    "contradicting enqueue should return false");
    TEST_ASSERT(solver.trail_size == size_before, "trail must not grow on contradiction");
    TEST_ASSERT(solver.assignment[1] == TRUE,   "assignment must remain TRUE");

    DESTROY_SOLVER(&solver);
}

/* ── 1.5 ── */
static void enqueue_with_reason_clause(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Create a dummy clause and pass as reason */
    int lits[] = {-1, 2};
    Clause* reason = make_clause(lits, 2, false);

    ENQUEUE(&solver, 2, reason);

    TEST_ASSERT(solver.reason[2] == reason, "reason pointer not stored correctly");

    DESTROY_SOLVER(&solver);
    free_clause(reason);
}

/* ── 1.6 ── */
static void enqueue_at_nonzero_level(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 3;
    ENQUEUE(&solver, 2, NO_REASON);

    TEST_ASSERT(solver.decision_level[2] == 3,
                "decision_level should match current_level at enqueue time");

    DESTROY_SOLVER(&solver);
}

/* ── 1.7 ── */
static void enqueue_trail_grows_dynamically(void) {
    /* Use a larger solver so we have enough variables to enqueue */
    int n = 20;
    CDCLSolver solver;
    int rc = make_solver(&solver, n, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /*
     * Artificially shrink trail_capacity to 2 so the buffer must reallocate
     * multiple times as we enqueue all 20 literals.
     */
    solver.trail_capacity = 2;

    bool all_ok = true;
    for (int v = 1; v <= n; v++) {
        if (!ENQUEUE(&solver, v, NO_REASON)) {
            all_ok = false;
        }
    }

    TEST_ASSERT(all_ok,                    "all enqueues should succeed");
    TEST_ASSERT(solver.trail_size == n,    "trail_size should equal n after all enqueues");
    TEST_ASSERT(solver.trail_capacity >= n,"trail_capacity must be >= n after realloc");

    /* Verify all literals are in the trail */
    for (int i = 0; i < n; i++) {
        TEST_ASSERT(solver.trail[i] == i + 1,
                    "trail literal mismatch after dynamic resize");
    }

    DESTROY_SOLVER(&solver);
}

/* ── main ── */
int main(void) {
    printf("=== T1: ENQUEUE + Trail Integrity ===\n");
    RUN_TEST(enqueue_positive_literal);
    RUN_TEST(enqueue_negative_literal);
    RUN_TEST(enqueue_already_consistent);
    RUN_TEST(enqueue_contradiction);
    RUN_TEST(enqueue_with_reason_clause);
    RUN_TEST(enqueue_at_nonzero_level);
    RUN_TEST(enqueue_trail_grows_dynamically);
    REPORT_AND_EXIT();
}
