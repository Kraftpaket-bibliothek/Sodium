/**
 * test_analyze_conflict.c  ── T3: ANALYZE_CONFLICT (1-UIP Conflict Analysis)
 * ===========================================================================
 * Tests the 1-UIP conflict analysis routine in isolation by pre-loading
 * assignments and reason clauses directly onto the trail and invoking
 * ANALYZE_CONFLICT.
 */

#include "test_harness.h"

static void manually_assign(CDCLSolver* s, int literal, int level, ClauseRef reason) {
    int var = abs(literal);
    s->assignment[var]     = (literal > 0) ? TRUE : FALSE;
    s->decision_level[var] = level;
    s->reason[var]         = reason;
    s->trail[s->trail_size++] = literal;
}

/* ── 3.1 ── */
static void analyze_conflict_unit_learned_clause(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 1, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    manually_assign(&solver, 1, 1, NO_REASON);

    int lits[] = {-1};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 1, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(backtrack_level == 0,     "backtrack level should be 0");
    TEST_ASSERT(learned != CLAUSE_REF_NULL, "learned clause must not be NULL");
    TEST_ASSERT(clause_array_get_size(&solver.arena, learned) == 1, "learned clause should have 1 literal");
    TEST_ASSERT(clause_array_get_literal(&solver.arena, learned, 0) == -1, "asserting literal should be -1");

    DESTROY_SOLVER(&solver);
}

/* ── 3.2 ── */
static void analyze_conflict_correct_asserting_literal(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 2;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);

    int lits[] = {-1, -2};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 2, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != CLAUSE_REF_NULL, "learned clause must not be NULL");
    TEST_ASSERT(clause_array_get_literal(&solver.arena, learned, 0) == -2, "asserting literal should be -2");
    TEST_ASSERT(backtrack_level == 1,        "backtrack level should be 1");

    DESTROY_SOLVER(&solver);
}

/* ── 3.3 ── */
static void analyze_conflict_backtrack_level_is_second_highest(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 3;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);
    manually_assign(&solver, 3, 3, NO_REASON);

    int lits[] = {-1, -2, -3};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 3, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != CLAUSE_REF_NULL, "learned clause must not be NULL");
    TEST_ASSERT(backtrack_level == 2, "backtrack level should be 2 (second-highest)");
    TEST_ASSERT(clause_array_get_literal(&solver.arena, learned, 0) == -3,
                "asserting literal (-3) should be at index 0");

    DESTROY_SOLVER(&solver);
}

/* ── 3.4 ── */
static void analyze_conflict_no_duplicate_literals(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 2;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 1, NO_REASON);
    manually_assign(&solver, 3, 2, NO_REASON);

    int lits[] = {-1, -2, -1, -3};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 4, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != CLAUSE_REF_NULL, "learned clause must not be NULL");
    TEST_ASSERT(backtrack_level >= 0, "backtrack level should be non-negative");

    int sz = clause_array_get_size(&solver.arena, learned);
    for (int i = 1; i < sz; i++) {
        for (int j = i + 1; j < sz; j++) {
            TEST_ASSERT(clause_array_get_literal(&solver.arena, learned, i) !=
                        clause_array_get_literal(&solver.arena, learned, j),
                        "learned clause must not have duplicate literals");
        }
    }

    DESTROY_SOLVER(&solver);
}

/* ── 3.5 ── */
static void analyze_conflict_second_watch_placed_at_index_1(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 4, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 4;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);
    manually_assign(&solver, 3, 3, NO_REASON);
    manually_assign(&solver, 4, 4, NO_REASON);

    int lits[] = {-1, -2, -3, -4};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 4, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    int backtrack_level = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != CLAUSE_REF_NULL, "learned clause must not be NULL");
    TEST_ASSERT(backtrack_level == 3, "backtrack level should be 3");
    TEST_ASSERT(clause_array_get_literal(&solver.arena, learned, 0) == -4, "asserting literal should be -4");
    TEST_ASSERT(clause_array_get_literal(&solver.arena, learned, 1) == -3,
                "highest non-current level literal should be at index 1");

    DESTROY_SOLVER(&solver);
}

/* ── 3.6 ── */
static void analyze_conflict_level_zero_skipped(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 2;
    manually_assign(&solver, 1, 0, NO_REASON);
    manually_assign(&solver, 2, 1, NO_REASON);
    manually_assign(&solver, 3, 2, NO_REASON);

    int lits[] = {-1, -2, -3};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 3, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(learned != CLAUSE_REF_NULL, "learned clause must not be NULL");

    int sz = clause_array_get_size(&solver.arena, learned);
    bool has_minus_one = false;
    for (int i = 0; i < sz; i++) {
        if (clause_array_get_literal(&solver.arena, learned, i) == -1) {
            has_minus_one = true;
            break;
        }
    }
    TEST_ASSERT(!has_minus_one, "level-0 literal -1 must not appear in learned clause");

    DESTROY_SOLVER(&solver);
}

/* ── 3.7 ── */
static void analyze_conflict_seen_array_cleared(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 3, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 3;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 2, NO_REASON);
    manually_assign(&solver, 3, 3, NO_REASON);

    int lits[] = {-1, -2, -3};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 3, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    ANALYZE_CONFLICT(&solver, conflict, &learned);

    for (int i = 1; i <= solver.number_of_variables; i++) {
        TEST_ASSERT(solver.seen[i] == false, "seen[i] must be cleared after ANALYZE_CONFLICT");
    }

    DESTROY_SOLVER(&solver);
}

/* ── 3.8 ── */
static void analyze_conflict_invalid_decision_no_reason(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 2, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    manually_assign(&solver, 1, 1, NO_REASON);
    manually_assign(&solver, 2, 1, NO_REASON);

    int lits[] = {-1, -2};
    ClauseRef conflict = clause_array_allocate(&solver.arena, lits, 2, false, 0.0f);

    ClauseRef learned = CLAUSE_REF_NULL;
    int result = ANALYZE_CONFLICT(&solver, conflict, &learned);

    TEST_ASSERT(result == ANALYSIS_ERROR, "should return ANALYSIS_ERROR");
    TEST_ASSERT(learned == CLAUSE_REF_NULL, "out_learned should be NULL on error");

    DESTROY_SOLVER(&solver);
}

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
