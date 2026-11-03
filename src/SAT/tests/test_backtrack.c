/**
 * test_backtrack.c  ── T4: BACKTRACK (Non-Chronological + Phase Saving)
 * ======================================================================
 * Tests the BACKTRACK function: unassigning variables above target_level,
 * adjusting trail / trail_limits, clamping propagation_head, saving polarity,
 * and error handling.
 *
 * Tests
 * -----
 * 4.1  backtrack_to_level_zero        – all vars unassigned, trail empty
 * 4.2  backtrack_to_middle_level      – only upper-level vars unassigned
 * 4.3  backtrack_noop_at_same_level   – target == current → SUCCESS, no change
 * 4.4  backtrack_invalid_target_above – target > current → ERROR
 * 4.5  backtrack_polarity_saved       – saved_polarity captures last assignment
 * 4.6  backtrack_propagation_head_clamped – head never exceeds trail_size
 * 4.7  backtrack_trail_limits_consistency – trail_limits_size decremented correctly
 */

#include "test_harness.h"

/* ── helper: push a new decision level ── */
static void push_decision(CDCLSolver* s, int literal) {
    s->current_level++;
    /* Record the start index of this level in trail_limits */
    s->trail_limits[s->trail_limits_size++] = s->trail_size;
    ENQUEUE(s, literal, NO_REASON);
}

/* ── 4.1 ── */
static void backtrack_to_level_zero(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision(&solver, 1);   /* level 1: var1 = TRUE  */
    push_decision(&solver, -2);  /* level 2: var2 = FALSE */
    push_decision(&solver, 3);   /* level 3: var3 = TRUE  */

    int ret = BACKTRACK(&solver, 0);

    TEST_ASSERT(ret == SUCCESS,                      "BACKTRACK should return SUCCESS");
    TEST_ASSERT(solver.current_level   == 0,         "current_level should be 0");
    TEST_ASSERT(solver.trail_size      == 0,         "trail should be empty");
    TEST_ASSERT(solver.trail_limits_size == 0,       "trail_limits should be empty");
    TEST_ASSERT(solver.assignment[1]   == UNASSIGNED,"var1 should be UNASSIGNED");
    TEST_ASSERT(solver.assignment[2]   == UNASSIGNED,"var2 should be UNASSIGNED");
    TEST_ASSERT(solver.assignment[3]   == UNASSIGNED,"var3 should be UNASSIGNED");

    DESTROY_SOLVER(&solver);
}

/* ── 4.2 ── */
static void backtrack_to_middle_level(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision(&solver, 1);   /* level 1: var1 = TRUE  */
    push_decision(&solver, 2);   /* level 2: var2 = TRUE  */
    push_decision(&solver, 3);   /* level 3: var3 = TRUE  */

    int ret = BACKTRACK(&solver, 1);

    TEST_ASSERT(ret == SUCCESS,                       "BACKTRACK should return SUCCESS");
    TEST_ASSERT(solver.current_level  == 1,           "current_level should be 1");
    /* var1 (level 1) must remain assigned */
    TEST_ASSERT(solver.assignment[1] == TRUE,         "var1 (level 1) must remain TRUE");
    /* var2 and var3 (levels 2, 3) must be unassigned */
    TEST_ASSERT(solver.assignment[2] == UNASSIGNED,   "var2 (level 2) should be UNASSIGNED");
    TEST_ASSERT(solver.assignment[3] == UNASSIGNED,   "var3 (level 3) should be UNASSIGNED");
    /* trail_limits: only level 1 entry should remain */
    TEST_ASSERT(solver.trail_limits_size == 1,        "trail_limits_size should be 1");

    DESTROY_SOLVER(&solver);
}

/* ── 4.3 ── */
static void backtrack_noop_at_same_level(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision(&solver, 1);   /* level 1 */
    push_decision(&solver, 2);   /* level 2 */

    int trail_before   = solver.trail_size;
    int limits_before  = solver.trail_limits_size;

    int ret = BACKTRACK(&solver, 2);   /* target == current: no-op */

    TEST_ASSERT(ret == SUCCESS,                          "BACKTRACK(current) should return SUCCESS");
    TEST_ASSERT(solver.current_level    == 2,            "current_level should remain 2");
    TEST_ASSERT(solver.trail_size       == trail_before, "trail_size must not change");
    TEST_ASSERT(solver.trail_limits_size == limits_before,"trail_limits_size must not change");

    DESTROY_SOLVER(&solver);
}

/* ── 4.4 ── */
static void backtrack_invalid_target_above(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision(&solver, 1);   /* level 1 */
    push_decision(&solver, 2);   /* level 2 */

    int ret = BACKTRACK(&solver, 5);   /* target > current → ERROR */

    TEST_ASSERT(ret == ERROR, "BACKTRACK to level above current should return ERROR");

    DESTROY_SOLVER(&solver);
}

/* ── 4.5 ── */
static void backtrack_polarity_saved(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision(&solver, 1);   /* level 1: var1 = TRUE  */
    push_decision(&solver, -2);  /* level 2: var2 = FALSE */

    BACKTRACK(&solver, 0);

    /* Phase saving: polarity at the time of unassignment must be remembered */
    TEST_ASSERT(solver.saved_polarity[1] == TRUE,
                "saved_polarity[1] should be TRUE after backtracking past level 1");
    TEST_ASSERT(solver.saved_polarity[2] == FALSE,
                "saved_polarity[2] should be FALSE after backtracking past level 2");

    DESTROY_SOLVER(&solver);
}

/* ── 4.6 ── */
static void backtrack_propagation_head_clamped(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision(&solver, 1);   /* level 1 */
    push_decision(&solver, 2);   /* level 2 */
    push_decision(&solver, 3);   /* level 3: trail_size == 3 */

    /*
     * Simulate propagation_head having advanced past what will remain after
     * backtracking to level 1 (trail_size will drop to 1).
     */
    solver.propagation_head = 3;

    BACKTRACK(&solver, 1);

    TEST_ASSERT(solver.propagation_head <= solver.trail_size,
                "propagation_head must not exceed trail_size after backtrack");

    DESTROY_SOLVER(&solver);
}

/* ── 4.7 ── */
static void backtrack_trail_limits_consistency(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 4, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    push_decision(&solver, 1);   /* level 1 */
    push_decision(&solver, 2);   /* level 2 */
    push_decision(&solver, 3);   /* level 3 */
    push_decision(&solver, 4);   /* level 4 */

    TEST_ASSERT(solver.trail_limits_size == 4, "should have 4 limit entries before backtrack");

    BACKTRACK(&solver, 2);

    /* Peeled 2 levels (4 → 2), so 2 entries removed */
    TEST_ASSERT(solver.trail_limits_size == 2,
                "trail_limits_size should be 2 after backtracking from level 4 to 2");
    TEST_ASSERT(solver.current_level == 2,
                "current_level should be 2 after backtrack");

    DESTROY_SOLVER(&solver);
}

/* ── main ── */
int main(void) {
    printf("=== T4: BACKTRACK ===\n");
    RUN_TEST(backtrack_to_level_zero);
    RUN_TEST(backtrack_to_middle_level);
    RUN_TEST(backtrack_noop_at_same_level);
    RUN_TEST(backtrack_invalid_target_above);
    RUN_TEST(backtrack_polarity_saved);
    RUN_TEST(backtrack_propagation_head_clamped);
    RUN_TEST(backtrack_trail_limits_consistency);
    REPORT_AND_EXIT();
}
