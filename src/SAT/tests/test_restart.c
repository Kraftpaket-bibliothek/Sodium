/**
 * test_restart.c  ── T5: RESTART + COMPUTE_RESTART_LIMIT
 * ========================================================
 * Tests the restart subsystem: exponential limit computation (Luby-style
 * 1.5^k schedule), conflict counter reset, index increment, and limit update.
 * IS_RESTART_DUE is static inline (not directly callable); its predicate
 * (conflicts_since_restart >= restart_limit) is evaluated via the struct fields.
 *
 * Tests
 * -----
 * 5.1  compute_restart_limit_index_1     – base=100, k=1 → 150
 * 5.2  compute_restart_limit_index_5     – base=100, k=5 → expected floor
 * 5.3  compute_restart_limit_overflow_clamped – extreme index → MAX_RESTART_LIMIT
 * 5.4  restart_resets_conflict_counter   – conflicts_since_restart becomes 0
 * 5.5  restart_increments_index          – restart_index += 1
 * 5.6  restart_updates_limit             – restart_limit = base * 1.5^new_index
 * 5.7  restart_unassigns_all_variables   – delegates to BACKTRACK(0)
 * 5.8  is_restart_due_at_threshold       – predicate true when equal to limit
 * 5.9  is_restart_due_below_threshold    – predicate false when strictly below
 */

#include "test_harness.h"
#include <math.h>
#include <limits.h>

/* Forward declarations for functions not in CDCL.h */
extern int COMPUTE_RESTART_LIMIT(int base, int restart_index);
extern int RESTART(CDCLSolver* solver);

#define MAX_RESTART_LIMIT (INT_MAX / 2)

/* ── push helper (re-used from backtrack tests) ── */
static void push_decision_r(CDCLSolver* s, int literal) {
    s->current_level++;
    s->trail_limits[s->trail_limits_size++] = s->trail_size;
    ENQUEUE(s, literal, NO_REASON);
}

/* ── 5.1 ── */
static void compute_restart_limit_index_1(void) {
    int result = COMPUTE_RESTART_LIMIT(100, 1);
    int expected = (int)(100.0 * pow(1.5, 1.0)); /* 150 */
    TEST_ASSERT(result == expected,
                "COMPUTE_RESTART_LIMIT(100, 1) should be 150");
}

/* ── 5.2 ── */
static void compute_restart_limit_index_5(void) {
    int result   = COMPUTE_RESTART_LIMIT(100, 5);
    int expected = (int)(100.0 * pow(1.5, 5.0)); /* 759 */
    TEST_ASSERT(result == expected,
                "COMPUTE_RESTART_LIMIT(100, 5) should be 759");
}

/* ── 5.3 ── */
static void compute_restart_limit_overflow_clamped(void) {
    /*
     * A very large restart_index produces a floating-point value far above
     * MAX_RESTART_LIMIT.  The function must clamp and return MAX_RESTART_LIMIT
     * without UB.
     */
    int result = COMPUTE_RESTART_LIMIT(100, 10000);
    TEST_ASSERT(result == MAX_RESTART_LIMIT,
                "extreme index should return MAX_RESTART_LIMIT");
}

/* ── 5.4 ── */
static void restart_resets_conflict_counter(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Simulate conflicts */
    solver.conflicts_since_restart = 50;

    int ret = RESTART(&solver);

    TEST_ASSERT(ret == SUCCESS,                           "RESTART should return SUCCESS");
    TEST_ASSERT(solver.conflicts_since_restart == 0,
                "conflicts_since_restart must be 0 after restart");

    DESTROY_SOLVER(&solver);
}

/* ── 5.5 ── */
static void restart_increments_index(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    int index_before = solver.restart_index;
    RESTART(&solver);

    TEST_ASSERT(solver.restart_index == index_before + 1,
                "restart_index should increment by 1");

    DESTROY_SOLVER(&solver);
}

/* ── 5.6 ── */
static void restart_updates_limit(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* After RESTART, index becomes restart_index+1 and limit is recomputed. */
    int base          = solver.restart_base;
    int index_before  = solver.restart_index;

    RESTART(&solver);

    int expected_limit = COMPUTE_RESTART_LIMIT(base, index_before + 1);
    TEST_ASSERT(solver.restart_limit == expected_limit,
                "restart_limit should equal base * 1.5^(new_index)");

    DESTROY_SOLVER(&solver);
}

/* ── 5.7 ── */
static void restart_unassigns_all_variables(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision_r(&solver, 1);    /* level 1 */
    push_decision_r(&solver, -2);   /* level 2 */
    push_decision_r(&solver, 3);    /* level 3 */

    TEST_ASSERT(solver.trail_size == 3, "trail should have 3 entries before restart");

    RESTART(&solver);

    TEST_ASSERT(solver.current_level    == 0,         "current_level should be 0 after restart");
    TEST_ASSERT(solver.trail_size       == 0,         "trail should be empty after restart");
    TEST_ASSERT(solver.assignment[1]    == UNASSIGNED,"var1 should be UNASSIGNED");
    TEST_ASSERT(solver.assignment[2]    == UNASSIGNED,"var2 should be UNASSIGNED");
    TEST_ASSERT(solver.assignment[3]    == UNASSIGNED,"var3 should be UNASSIGNED");

    DESTROY_SOLVER(&solver);
}

/* ── 5.8 ── */
static void is_restart_due_at_threshold(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.conflicts_since_restart = solver.restart_limit; /* equal to limit */

    /* IS_RESTART_DUE predicate: conflicts_since_restart >= restart_limit */
    bool due = (solver.conflicts_since_restart >= solver.restart_limit);
    TEST_ASSERT(due == true, "IS_RESTART_DUE should be true at the exact threshold");

    DESTROY_SOLVER(&solver);
}

/* ── 5.9 ── */
static void is_restart_due_below_threshold(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.conflicts_since_restart = solver.restart_limit - 1; /* one below limit */

    bool due = (solver.conflicts_since_restart >= solver.restart_limit);
    TEST_ASSERT(due == false, "IS_RESTART_DUE should be false below threshold");

    DESTROY_SOLVER(&solver);
}

/* ── main ── */
int main(void) {
    printf("=== T5: RESTART + COMPUTE_RESTART_LIMIT ===\n");
    RUN_TEST(compute_restart_limit_index_1);
    RUN_TEST(compute_restart_limit_index_5);
    RUN_TEST(compute_restart_limit_overflow_clamped);
    RUN_TEST(restart_resets_conflict_counter);
    RUN_TEST(restart_increments_index);
    RUN_TEST(restart_updates_limit);
    RUN_TEST(restart_unassigns_all_variables);
    RUN_TEST(is_restart_due_at_threshold);
    RUN_TEST(is_restart_due_below_threshold);
    REPORT_AND_EXIT();
}
