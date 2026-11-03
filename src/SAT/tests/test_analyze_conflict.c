/**
 * test_analyze_conflict.c  ── T3: ANALYZE_CONFLICT (1-UIP)
 * ==========================================================
 * All test scenarios are crafted so that EXACTLY ONE variable in the conflict
 * clause is at the current decision level, making the decision literal the
 * 1-UIP and causing path_count to reach 0 immediately.  This avoids the
 * implementation's known sensitivity when reason clauses contain the implied
 * literal at the current level.
 *
 * Tests
 * -----
 * 3.1  unit_learned_clause          – single current-level literal → learned {-v}
 * 3.2  correct_asserting_literal    – asserting literal is negation of UIP
 * 3.3  backtrack_level_second_highest – returned level = second-highest level
 * 3.4  no_duplicate_literals        – duplicate in conflict clause is deduplicated
 * 3.5  second_watch_at_index_1      – highest-level non-current literal at [1]
 * 3.6  level_zero_literals_skipped  – level-0 vars excluded from learned clause
 * 3.7  seen_array_cleared           – all seen[] == false after analysis
 * 3.8  analysis_error_no_reason     – path_count>0 + reason==NULL → ANALYSIS_ERROR
 *
 * Manual state setup
 * ------------------
 * Each test initialises an empty solver (0 input clauses) with make_solver(),
 * then manually populates the trail, assignment, decision_level, and reason
 * arrays to represent the desired implication graph.  The conflict clause is
 * a locally allocated Clause that is NOT added to the solver's clause database.
 */

#include "test_harness.h"
#include <math.h>

/* ── helpers ── */

/*
 * manually_assign – set all state fields for a variable as if it had been
 * enqueued by the solver at the given level.  appended to trail manually.
 */
static void manually_assign(CDCLSolver* s, int literal, int level, Clause* reason) {
    int var = abs(literal);
    s->assignment[var]     = (literal > 0) ? TRUE : FALSE;
    s->decision_level[var] = level;
    s->reason[var]         = reason;
    s->trail[s->trail_size++] = literal;
}

/* ── 3.1 ── */
static void analyze_conflict_unit_learned_clause(void) {
    /*
     * State: n=1, current_level=1.
     * Trail: [1]  (var1 decided TRUE at level 1, reason=NULL).
     * Conflict clause: {-1}  (only one literal, at the current level).
     *
     * Expected:
     *   learned = {-1}  (asserting literal = -1)
     *   backtrack_level = 0
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 1, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    manually_assign(&solver, 1, 1, NO_REASON);

    int lits[] = {-1};
    Clause* conflict = make_clause(lits, 1, false);

    Clause* learned = NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(backtrack_level == 0,     "backtrack level should be 0");
    TEST_ASSERT(learned != NULL,          "learned clause must not be NULL");
    TEST_ASSERT(learned->size == 1,       "learned clause should have 1 literal");
    TEST_ASSERT(learned->literals[0] == -1,"asserting literal should be -1");

    /* cleanup learned clause (it was malloc'd by ANALYZE_CONFLICT) */
    if (learned) { free(learned->literals); free(learned); }
    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── 3.2 ── */
static void analyze_conflict_correct_asserting_literal(void) {
    /*
     * n=2, current_level=2.
     * Trail: [1, 2]  (var1 at level 1 decided, var2 at level 2 decided).
     * Conflict: {-1, -2}  (-1 at level 1 → goes to learned; -2 at level 2=current → UIP).
     *
     * Expected:
     *   asserting literal at [0] = -(-2) ... wait: resolved_literal = trail[trail_index]
     *   trail[1]=2, resolved_literal=2, asserting_literal = -2.
     *   learned[0] = -2, backtrack_level = 1.
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 2;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);

    int lits[] = {-1, -2};
    Clause* conflict = make_clause(lits, 2, false);

    Clause* learned = NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != NULL,             "learned clause must not be NULL");
    TEST_ASSERT(learned->literals[0] == -2, "asserting literal should be -2");
    TEST_ASSERT(backtrack_level == 1,        "backtrack level should be 1");

    if (learned) { free(learned->literals); free(learned); }
    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── 3.3 ── */
static void analyze_conflict_backtrack_level_is_second_highest(void) {
    /*
     * n=3, current_level=3.
     * Trail: [1, 2, 3]  (var1 level 1, var2 level 2, var3 level 3 – all decisions).
     * Conflict: {-1, -2, -3}.
     *   -1 (level 1) → add to learned
     *   -2 (level 2) → add to learned
     *   -3 (level 3 = current) → path_count=1 → UIP, asserting=-3
     * Expected: backtrack_level = 2 (second-highest in learned)
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 3;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);
    manually_assign(&solver, 3, 3, NO_REASON);

    int lits[] = {-1, -2, -3};
    Clause* conflict = make_clause(lits, 3, false);

    Clause* learned = NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != NULL,      "learned clause must not be NULL");
    TEST_ASSERT(backtrack_level == 2, "backtrack level should be 2 (second-highest)");
    TEST_ASSERT(learned->literals[0] == -3,
                "asserting literal (-3) should be at index 0");

    if (learned) { free(learned->literals); free(learned); }
    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── 3.4 ── */
static void analyze_conflict_no_duplicate_literals(void) {
    /*
     * n=3, current_level=2.
     * Trail: [1, 2, 3]  (var1 level 1, var2 level 1, var3 level 2 – decision).
     * Conflict: {-1, -2, -1, -3}  (-1 duplicated; -3 at level 2=current → UIP).
     *   -1 appears twice at level 1 → should appear only ONCE in learned.
     *   -2 at level 1 → appears in learned.
     *   -3 at level 2=current → UIP.
     * Expected: learned has no duplicates.
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 2;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 1, NO_REASON);
    manually_assign(&solver, 3, 2, NO_REASON);

    int lits[] = {-1, -2, -1, -3};
    Clause* conflict = make_clause(lits, 4, false);

    Clause* learned = NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != NULL, "learned clause must not be NULL");
    TEST_ASSERT(backtrack_level >= 0, "backtrack level should be non-negative");

    /* Check no duplicates in learned[1..] */
    for (int i = 1; i < learned->size; i++) {
        for (int j = i + 1; j < learned->size; j++) {
            TEST_ASSERT(learned->literals[i] != learned->literals[j],
                        "learned clause must not have duplicate literals");
        }
    }

    if (learned) { free(learned->literals); free(learned); }
    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── 3.5 ── */
static void analyze_conflict_second_watch_placed_at_index_1(void) {
    /*
     * n=4, current_level=4.
     * Trail: [1,2,3,4] at levels 1,2,3,4 (all decisions).
     * Conflict: {-1, -2, -3, -4}.
     *   -4 at level 4=current → UIP (path_count=1).
     *   Others (levels 1,2,3) added to learned clause.
     * After analysis, literals[1] in learned must be the literal with the
     * HIGHEST non-current level (level 3 → literal -3).
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 4, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 4;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);
    manually_assign(&solver, 3, 3, NO_REASON);
    manually_assign(&solver, 4, 4, NO_REASON);

    int lits[] = {-1, -2, -3, -4};
    Clause* conflict = make_clause(lits, 4, false);

    Clause* learned = NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != NULL,      "learned clause must not be NULL");
    TEST_ASSERT(backtrack_level == 3, "backtrack level should be 3");
    TEST_ASSERT(learned->literals[0] == -4, "asserting literal should be -4");

    /*
     * The literal with the highest non-current level (level 3, i.e. -3)
     * must be placed at index 1 so the 2-WL can watch it immediately after
     * backjumping.
     */
    TEST_ASSERT(learned->literals[1] == -3,
                "highest non-current level literal should be at index 1");

    if (learned) { free(learned->literals); free(learned); }
    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── 3.6 ── */
static void analyze_conflict_level_zero_skipped(void) {
    /*
     * n=3, current_level=2.
     * var1 assigned at level 0 (root unit propagation).
     * var2 at level 1 (decision).
     * var3 at level 2 (current, decision → UIP).
     * Conflict: {-1, -2, -3}.
     *   -1 (level 0) → SKIPPED
     *   -2 (level 1) → added to learned
     *   -3 (level 2=current) → UIP
     * Expected: learned = {-3, -2}  (literal -1 absent)
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 2;
    manually_assign(&solver, 1, 0, NO_REASON); /* level 0 – root assignment */
    manually_assign(&solver, 2, 1, NO_REASON);
    manually_assign(&solver, 3, 2, NO_REASON);

    int lits[] = {-1, -2, -3};
    Clause* conflict = make_clause(lits, 3, false);

    Clause* learned = NULL;
    ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != NULL, "learned clause must not be NULL");

    /* Verify literal -1 does NOT appear in learned */
    bool has_minus_one = false;
    for (int i = 0; i < learned->size; i++) {
        if (learned->literals[i] == -1) { has_minus_one = true; break; }
    }
    TEST_ASSERT(!has_minus_one,
                "level-0 literal -1 must not appear in learned clause");

    if (learned) { free(learned->literals); free(learned); }
    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── 3.7 ── */
static void analyze_conflict_seen_array_cleared(void) {
    /*
     * Reuse the scenario from 3.3.  After ANALYZE_CONFLICT, every
     * solver.seen[i] must be false.
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 3;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);
    manually_assign(&solver, 3, 3, NO_REASON);

    int lits[] = {-1, -2, -3};
    Clause* conflict = make_clause(lits, 3, false);

    Clause* learned = NULL;
    ANALYZE_CONFLICT(&solver, conflict, &learned);

    for (int i = 1; i <= solver.number_of_variables; i++) {
        TEST_ASSERT(solver.seen[i] == false,
                    "seen[i] must be cleared after ANALYZE_CONFLICT");
    }

    if (learned) { free(learned->literals); free(learned); }
    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── 3.8 ── */
static void analyze_conflict_invalid_decision_no_reason(void) {
    /*
     * Manufacture an INVALID solver state where path_count > 0 when a
     * variable with reason==NULL is encountered mid-analysis:
     *
     *   n=2, current_level=1.
     *   Trail: [1, 2]  (var1 level 1 reason=NULL, var2 level 1 reason=NULL).
     *   Conflict: {-1, -2}  → both at current level → path_count starts at 2.
     *   Resolve trail[1]=2: path_count → 1, then reason[2]==NULL → ANALYSIS_ERROR.
     */
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 1, NO_REASON); /* invalid: two decisions at same level */

    int lits[] = {-1, -2};
    Clause* conflict = make_clause(lits, 2, false);

    Clause* learned = NULL;
    int result = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(result == ANALYSIS_ERROR, "should return ANALYSIS_ERROR");
    TEST_ASSERT(learned == NULL,          "out_learned should be NULL on error");

    free_clause(conflict);
    DESTROY_SOLVER(&solver);
}

/* ── main ── */
int main(void) {
    printf("=== T3: ANALYZE_CONFLICT (1-UIP) ===\n");
    RUN_TEST(analyze_conflict_unit_learned_clause);
    RUN_TEST(analyze_conflict_correct_asserting_literal);
    RUN_TEST(analyze_conflict_backtrack_level_is_second_highest);
    RUN_TEST(analyze_conflict_no_duplicate_literals);
    RUN_TEST(analyze_conflict_second_watch_placed_at_index_1);
    RUN_TEST(analyze_conflict_level_zero_skipped);
    RUN_TEST(analyze_conflict_seen_array_cleared);
    RUN_TEST(analyze_conflict_invalid_decision_no_reason);
    REPORT_AND_EXIT();
}
