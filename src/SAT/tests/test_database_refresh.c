/**
 * test_database_refresh.c  ── T6: REFRESH_CLAUSE_DATABASE
 * =========================================================
 * Tests clause database maintenance / reduction: candidate ranking by
 * activity, deletion of the lowest-activity half, protection of reason / unit /
 * original clauses, watchlist detachment, database compaction, and conflict
 * threshold updates.
 *
 * Tests
 * -----
 * 6.1  refresh_deletes_half_candidates        – 10 learned clauses → 5 deleted
 * 6.2  refresh_keeps_reason_clauses           – reason clauses never deleted
 * 6.3  refresh_keeps_original_clauses         – original clauses untouched
 * 6.4  refresh_keeps_unit_learned_clauses     – unit learned clauses preserved
 * 6.5  refresh_removes_from_watchlist         – deleted clauses unlinked from 2-WL
 * 6.6  refresh_deletes_lowest_activity        – lowest-activity clauses targeted
 * 6.7  refresh_updates_next_refresh_conflict  – interval schedule recalculated
 * 6.8  refresh_compacts_clause_array          – no NULL gaps in clause array
 */

#include "test_harness.h"

/* Declarations for functions in CDCL.c not in CDCL.h */
extern int REFRESH_CLAUSE_DATABASE(CDCLSolver* solver);
extern bool IS_DATABASE_REFRESH_DUE(const CDCLSolver* solver);

/**
 * Helper to register a learned clause into the solver's database and watch lists.
 */
static void add_learned_clause(CDCLSolver* solver, Clause* c) {
    if (solver->clause_count >= solver->clause_capacity) {
        solver->clause_capacity = (solver->clause_capacity == 0) ? 16 : solver->clause_capacity * 2;
        solver->clauses = (Clause**)realloc(solver->clauses, solver->clause_capacity * sizeof(Clause*));
    }
    solver->clauses[solver->clause_count++] = c;
    solver->learned_clause_count++;
    ATTACH_CLAUSE(solver, c);
}

/* ── 6.1 ── */
static void refresh_deletes_half_candidates(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 10, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Add 10 2-literal learned clauses with distinct activities */
    for (int i = 1; i <= 10; i++) {
        int lits[2] = {i, (i % 10) + 1};
        Clause* c = make_clause(lits, 2, true);
        c->activity = (double)i;
        add_learned_clause(&solver, c);
    }

    TEST_ASSERT(solver.clause_count == 10, "should have 10 clauses before refresh");
    TEST_ASSERT(solver.learned_clause_count == 10, "learned count should be 10");

    int res = REFRESH_CLAUSE_DATABASE(&solver);

    TEST_ASSERT(res == SUCCESS, "REFRESH_CLAUSE_DATABASE should return SUCCESS");
    /* 10 candidates -> exactly 5 deleted */
    TEST_ASSERT(solver.clause_count == 5, "clause_count should be 5 after deleting half");
    TEST_ASSERT(solver.learned_clause_count == 5, "learned_clause_count should be 5");

    DESTROY_SOLVER(&solver);
}

/* ── 6.2 ── */
static void refresh_keeps_reason_clauses(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 10, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Clause 1: will be marked as a reason for variable 1 */
    int lits1[2] = {1, 2};
    Clause* c1 = make_clause(lits1, 2, true);
    c1->activity = 0.1; /* lowest activity, would normally be deleted first */
    add_learned_clause(&solver, c1);

    /* Clause 2: unreferenced, activity 1.0 */
    int lits2[2] = {2, 3};
    Clause* c2 = make_clause(lits2, 2, true);
    c2->activity = 1.0;
    add_learned_clause(&solver, c2);

    /* Clause 3: unreferenced, activity 2.0 */
    int lits3[2] = {3, 4};
    Clause* c3 = make_clause(lits3, 2, true);
    c3->activity = 2.0;
    add_learned_clause(&solver, c3);

    /* Assign variable 1 and set c1 as its reason */
    solver.assignment[1] = TRUE;
    solver.reason[1] = c1;

    /* Candidates are c2 and c3 (count = 2). c1 is skipped because it's a reason.
     * to_delete = 2 / 2 = 1 (c2 with activity 1.0 will be deleted).
     */
    REFRESH_CLAUSE_DATABASE(&solver);

    bool c1_present = false;
    for (int i = 0; i < solver.clause_count; i++) {
        if (solver.clauses[i] == c1) c1_present = true;
    }

    TEST_ASSERT(c1_present, "reason clause c1 must NOT be deleted despite low activity");

    DESTROY_SOLVER(&solver);
}

/* ── 6.3 ── */
static void refresh_keeps_original_clauses(void) {
    /* Create 1 original clause */
    int orig_lits[2] = {1, 2};
    Clause* orig = make_clause(orig_lits, 2, false);
    Clause* input_clauses[1] = {orig};

    CDCLSolver solver;
    int rc = make_solver(&solver, 6, input_clauses, 1);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Add 4 learned clauses */
    for (int i = 1; i <= 4; i++) {
        int lits[2] = {i + 1, i + 2};
        Clause* c = make_clause(lits, 2, true);
        c->activity = (double)i;
        add_learned_clause(&solver, c);
    }

    TEST_ASSERT(solver.clause_count == 5, "total 5 clauses initially (1 orig + 4 learned)");

    REFRESH_CLAUSE_DATABASE(&solver);

    /* 4 learned candidates -> 2 deleted -> 3 clauses remain (1 orig + 2 learned) */
    TEST_ASSERT(solver.clause_count == 3, "should have 3 clauses remaining");

    bool orig_found = false;
    for (int i = 0; i < solver.clause_count; i++) {
        if (solver.clauses[i] == orig) orig_found = true;
    }
    TEST_ASSERT(orig_found, "original clause must be preserved");

    DESTROY_SOLVER(&solver);
    free_clause(orig);
}

/* ── 6.4 ── */
static void refresh_keeps_unit_learned_clauses(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 6, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Unit learned clause */
    int unit_lit[1] = {1};
    Clause* unit_c = make_clause(unit_lit, 1, true);
    unit_c->activity = 0.01; /* lowest activity */
    add_learned_clause(&solver, unit_c);

    /* Two binary learned clauses */
    int lits2[2] = {2, 3};
    Clause* c2 = make_clause(lits2, 2, true);
    c2->activity = 1.0;
    add_learned_clause(&solver, c2);

    int lits3[2] = {4, 5};
    Clause* c3 = make_clause(lits3, 2, true);
    c3->activity = 2.0;
    add_learned_clause(&solver, c3);

    /* Candidates are c2 and c3 (unit_c has size <= 1 so ignored).
     * to_delete = 2 / 2 = 1.
     */
    REFRESH_CLAUSE_DATABASE(&solver);

    bool unit_found = false;
    for (int i = 0; i < solver.clause_count; i++) {
        if (solver.clauses[i] == unit_c) unit_found = true;
    }
    TEST_ASSERT(unit_found, "unit learned clause must be protected from refresh deletion");

    DESTROY_SOLVER(&solver);
}

/* ── 6.5 ── */
static void refresh_removes_from_watchlist(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 6, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Clause to be deleted (low activity) */
    int lits_del[2] = {1, 2};
    Clause* c_del = make_clause(lits_del, 2, true);
    c_del->activity = 1.0;
    add_learned_clause(&solver, c_del);

    /* Clause to be kept (higher activity) */
    int lits_keep[2] = {3, 4};
    Clause* c_keep = make_clause(lits_keep, 2, true);
    c_keep->activity = 10.0;
    add_learned_clause(&solver, c_keep);

    size_t idx1 = LIT_TO_INDEX(1, solver.number_of_variables);
    size_t idx2 = LIT_TO_INDEX(2, solver.number_of_variables);

    /* Verify c_del is in watch lists for literal 1 and 2 */
    bool in_watch1 = false, in_watch2 = false;
    for (int i = 0; i < solver.watch_list_sizes[idx1]; i++) {
        if (solver.watch_lists[idx1][i] == c_del) in_watch1 = true;
    }
    for (int i = 0; i < solver.watch_list_sizes[idx2]; i++) {
        if (solver.watch_lists[idx2][i] == c_del) in_watch2 = true;
    }
    TEST_ASSERT(in_watch1 && in_watch2, "c_del must be watched initially");

    /* Refresh deletes c_del */
    REFRESH_CLAUSE_DATABASE(&solver);

    /* Verify c_del is removed from watch lists */
    in_watch1 = false;
    in_watch2 = false;
    for (int i = 0; i < solver.watch_list_sizes[idx1]; i++) {
        if (solver.watch_lists[idx1][i] == c_del) in_watch1 = true;
    }
    for (int i = 0; i < solver.watch_list_sizes[idx2]; i++) {
        if (solver.watch_lists[idx2][i] == c_del) in_watch2 = true;
    }
    TEST_ASSERT(!in_watch1, "c_del must be removed from watch_lists[idx1]");
    TEST_ASSERT(!in_watch2, "c_del must be removed from watch_lists[idx2]");

    DESTROY_SOLVER(&solver);
}

/* ── 6.6 ── */
static void refresh_deletes_lowest_activity(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 10, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Create 4 clauses with activities 5.0, 1.0, 8.0, 2.0 */
    int l[4][2] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}};
    double acts[4] = {5.0, 1.0, 8.0, 2.0};
    Clause* cs[4];

    for (int i = 0; i < 4; i++) {
        cs[i] = make_clause(l[i], 2, true);
        cs[i]->activity = acts[i];
        add_learned_clause(&solver, cs[i]);
    }

    /* 4 candidates -> delete 2 lowest: cs[1] (1.0) and cs[3] (2.0) */
    REFRESH_CLAUSE_DATABASE(&solver);

    TEST_ASSERT(solver.clause_count == 2, "2 clauses should remain");

    bool kept_5 = false, kept_8 = false;
    for (int i = 0; i < solver.clause_count; i++) {
        if (solver.clauses[i]->activity == 5.0) kept_5 = true;
        if (solver.clauses[i]->activity == 8.0) kept_8 = true;
    }

    TEST_ASSERT(kept_5, "clause with activity 5.0 should be preserved");
    TEST_ASSERT(kept_8, "clause with activity 8.0 should be preserved");

    DESTROY_SOLVER(&solver);
}

/* ── 6.7 ── */
static void refresh_updates_next_refresh_conflict(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 4, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.conflict_count = 500;
    solver.refresh_interval = 1000;

    REFRESH_CLAUSE_DATABASE(&solver);

    TEST_ASSERT(solver.next_refresh_conflict == 1500,
                "next_refresh_conflict should be updated to 500 + 1000 = 1500");

    /* Test IS_DATABASE_REFRESH_DUE */
    solver.min_learned_clauses = 10;
    solver.learned_clause_count = 5;
    solver.conflict_count = 1600;
    TEST_ASSERT(!IS_DATABASE_REFRESH_DUE(&solver),
                "refresh not due when learned clauses < min_learned_clauses");

    solver.learned_clause_count = 15;
    TEST_ASSERT(IS_DATABASE_REFRESH_DUE(&solver),
                "refresh due when conflict_count >= next and learned >= min");

    DESTROY_SOLVER(&solver);
}

/* ── 6.8 ── */
static void refresh_compacts_clause_array(void) {
    CDCLSolver solver;
    int rc = make_solver(&solver, 8, NULL, 0);
    TEST_ASSERT(rc == 0, "make_solver failed");

    for (int i = 0; i < 4; i++) {
        int lits[2] = {i * 2 + 1, i * 2 + 2};
        Clause* c = make_clause(lits, 2, true);
        c->activity = (double)(i + 1);
        add_learned_clause(&solver, c);
    }

    REFRESH_CLAUSE_DATABASE(&solver);

    TEST_ASSERT(solver.clause_count == 2, "clause_count should be compacted to 2");
    for (int i = 0; i < solver.clause_count; i++) {
        TEST_ASSERT(solver.clauses[i] != NULL, "clauses array must have no NULL slots");
        TEST_ASSERT(!solver.clauses[i]->is_deleted, "compacted clauses must not be marked deleted");
    }

    DESTROY_SOLVER(&solver);
}

/* ── main ── */
int main(void) {
    printf("=== T6: REFRESH_CLAUSE_DATABASE ===\n");
    RUN_TEST(refresh_deletes_half_candidates);
    RUN_TEST(refresh_keeps_reason_clauses);
    RUN_TEST(refresh_keeps_original_clauses);
    RUN_TEST(refresh_keeps_unit_learned_clauses);
    RUN_TEST(refresh_removes_from_watchlist);
    RUN_TEST(refresh_deletes_lowest_activity);
    RUN_TEST(refresh_updates_next_refresh_conflict);
    RUN_TEST(refresh_compacts_clause_array);
    REPORT_AND_EXIT();
}
