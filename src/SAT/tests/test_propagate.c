/**
 * test_propagate.c  ── T2: PROPAGATE (Two-Watched-Literal BCP)
 * =============================================================
 * Tests the PROPAGATE function: empty-trail fast-path, unit clause forcing,
 * watch swapping, conflict detection, and propagation chains.
 *
 * Tests
 * -----
 * 2.1  propagate_empty_trail          – idle trail returns NO_CONFLICT
 * 2.2  propagate_single_unit_clause   – unit clause forces literal at level 0
 * 2.3  propagate_binary_unit          – binary clause with one FALSE watch
 *                                       forces the other
 * 2.4  propagate_conflict_detected    – both watches FALSE → conflict returned
 * 2.5  propagate_watch_swap_replacement – false watch replaced by free literal
 * 2.6  propagate_clause_already_satisfied – other watch TRUE; no propagation
 * 2.7  propagate_multiple_implications_chain – A→B→C via two binary clauses
 * 2.8  propagate_stops_at_first_conflict    – first conflict returned; loop stops
 */

#include "test_harness.h"

/* ── 2.1 ── */
static void propagate_empty_trail(void) {
    int lits[] = {1, 2};
    Clause* c = make_clause(lits, 2, false);
    Clause* clauses[] = {c};

    CDCLSolver solver;
    int rc = make_solver(&solver, 2, clauses, 1);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* Trail is empty (no unit clauses), propagation head is already at end */
    Clause* conflict = PROPAGATE(&solver);
    TEST_ASSERT(conflict == NO_CONFLICT, "expected NO_CONFLICT on empty trail");

    DESTROY_SOLVER(&solver);
    free_clause(c);
}

/* ── 2.2 ── */
static void propagate_single_unit_clause(void) {
    /*
     * Clause {1} is a unit clause. INITIALIZE_SOLVER calls
     * INITIALIZE_UNIT_PROPAGATION, which enqueues literal 1 before we reach
     * the SOLVE loop. PROPAGATE then advances propagation_head.
     */
    int lits[] = {1};
    Clause* c = make_clause(lits, 1, false);
    Clause* clauses[] = {c};

    CDCLSolver solver;
    int rc = make_solver(&solver, 2, clauses, 1);
    TEST_ASSERT(rc == 0, "make_solver failed");

    /* After INITIALIZE_SOLVER, literal 1 should already be enqueued */
    TEST_ASSERT(solver.assignment[1] == TRUE, "unit clause should force var1=TRUE");

    Clause* conflict = PROPAGATE(&solver);
    TEST_ASSERT(conflict == NO_CONFLICT, "unit clause: expected NO_CONFLICT");
    TEST_ASSERT(solver.propagation_head == solver.trail_size,
                "propagation_head should equal trail_size after full propagation");

    DESTROY_SOLVER(&solver);
    free_clause(c);
}

/* ── 2.3 ── */
static void propagate_binary_unit(void) {
    /*
     * Clause {1, 2}.  Manually enqueue -1 (var1=FALSE) at level 1 so that
     * PROPAGATE should unit-force literal 2.
     */
    int lits[] = {1, 2};
    Clause* c = make_clause(lits, 2, false);
    Clause* clauses[] = {c};

    CDCLSolver solver;
    int rc = make_solver(&solver, 2, clauses, 1);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    ENQUEUE(&solver, -1, NO_REASON);   /* var1 = FALSE */

    Clause* conflict = PROPAGATE(&solver);

    TEST_ASSERT(conflict == NO_CONFLICT,       "binary unit: expected NO_CONFLICT");
    TEST_ASSERT(solver.assignment[2] == TRUE,  "var2 should be forced TRUE");

    DESTROY_SOLVER(&solver);
    free_clause(c);
}

/* ── 2.4 ── */
static void propagate_conflict_detected(void) {
    /*
     * Clause {1, 2}.  Both var1=FALSE and var2=FALSE are on the trail.
     * PROPAGATE should return the clause as the conflict.
     */
    int lits[] = {1, 2};
    Clause* c = make_clause(lits, 2, false);
    Clause* clauses[] = {c};

    CDCLSolver solver;
    int rc = make_solver(&solver, 2, clauses, 1);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    /* Enqueue -2 first so that when -1 is processed its watch sees -2=FALSE */
    ENQUEUE(&solver, -2, NO_REASON);   /* var2 = FALSE */
    ENQUEUE(&solver, -1, NO_REASON);   /* var1 = FALSE */

    Clause* conflict = PROPAGATE(&solver);

    TEST_ASSERT(conflict != NO_CONFLICT, "expected a conflict clause");

    DESTROY_SOLVER(&solver);
    free_clause(c);
}

/* ── 2.5 ── */
static void propagate_watch_swap_replacement(void) {
    /*
     * Clause {1, 2, 3}.  Enqueue -1 (var1=FALSE).  The watch on literal 1
     * must be replaced by literal 2 or 3 (both UNASSIGNED).
     * No propagation should occur and NO_CONFLICT is returned.
     */
    int lits[] = {1, 2, 3};
    Clause* c = make_clause(lits, 3, false);
    Clause* clauses[] = {c};

    CDCLSolver solver;
    int rc = make_solver(&solver, 3, clauses, 1);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    ENQUEUE(&solver, -1, NO_REASON);   /* var1 = FALSE */

    Clause* conflict = PROPAGATE(&solver);

    TEST_ASSERT(conflict == NO_CONFLICT, "watch-swap: expected NO_CONFLICT");
    /* var2 and var3 must remain unassigned (no forced unit) */
    TEST_ASSERT(solver.assignment[2] == UNASSIGNED,
                "var2 should remain UNASSIGNED after watch swap");
    TEST_ASSERT(solver.assignment[3] == UNASSIGNED,
                "var3 should remain UNASSIGNED after watch swap");

    DESTROY_SOLVER(&solver);
    free_clause(c);
}

/* ── 2.6 ── */
static void propagate_clause_already_satisfied(void) {
    /*
     * Clause {1, 2}.  var2=TRUE (other watch satisfied).  Assign var1=FALSE.
     * PROPAGATE: the other watch is TRUE so the clause is satisfied; no new
     * implication and no conflict.
     */
    int lits[] = {1, 2};
    Clause* c = make_clause(lits, 2, false);
    Clause* clauses[] = {c};

    CDCLSolver solver;
    int rc = make_solver(&solver, 2, clauses, 1);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    ENQUEUE(&solver, 2, NO_REASON);    /* var2 = TRUE  (satisfies clause) */
    ENQUEUE(&solver, -1, NO_REASON);   /* var1 = FALSE (triggers watch)   */

    Clause* conflict = PROPAGATE(&solver);

    TEST_ASSERT(conflict == NO_CONFLICT,
                "satisfied clause: expected NO_CONFLICT");

    DESTROY_SOLVER(&solver);
    free_clause(c);
}

/* ── 2.7 ── */
static void propagate_multiple_implications_chain(void) {
    /*
     * Two clauses: {-1, 2} and {-2, 3}.
     * Enqueue literal 1 (var1=TRUE).  PROPAGATE should force: 1→2→3.
     */
    int lits1[] = {-1, 2};
    int lits2[] = {-2, 3};
    Clause* c1 = make_clause(lits1, 2, false);
    Clause* c2 = make_clause(lits2, 2, false);
    Clause* clauses[] = {c1, c2};

    CDCLSolver solver;
    int rc = make_solver(&solver, 3, clauses, 2);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    ENQUEUE(&solver, 1, NO_REASON);    /* Decision: var1 = TRUE */

    Clause* conflict = PROPAGATE(&solver);

    TEST_ASSERT(conflict == NO_CONFLICT,         "chain: expected NO_CONFLICT");
    TEST_ASSERT(solver.assignment[1] == TRUE,  "var1 should be TRUE");
    TEST_ASSERT(solver.assignment[2] == TRUE,  "var2 should be forced TRUE");
    TEST_ASSERT(solver.assignment[3] == TRUE,  "var3 should be forced TRUE");
    TEST_ASSERT(solver.trail_size    == 3,     "trail should have 3 literals");

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
}

/* ── 2.8 ── */
static void propagate_stops_at_first_conflict(void) {
    /*
     * Two potentially conflicting clauses: {1, 2} and {1, 3}.
     * Assign var1=FALSE, var2=FALSE, var3=FALSE so that the FIRST clause
     * encountered in the watch list triggers a conflict.  PROPAGATE must
     * return exactly one conflict clause (not loop endlessly).
     */
    int lits1[] = {1, 2};
    int lits2[] = {1, 3};
    Clause* c1 = make_clause(lits1, 2, false);
    Clause* c2 = make_clause(lits2, 2, false);
    Clause* clauses[] = {c1, c2};

    CDCLSolver solver;
    int rc = make_solver(&solver, 3, clauses, 2);
    TEST_ASSERT(rc == 0, "make_solver failed");

    solver.current_level = 1;
    ENQUEUE(&solver, -2, NO_REASON);   /* var2 = FALSE */
    ENQUEUE(&solver, -3, NO_REASON);   /* var3 = FALSE */
    ENQUEUE(&solver, -1, NO_REASON);   /* var1 = FALSE → triggers both clauses */

    Clause* conflict = PROPAGATE(&solver);

    TEST_ASSERT(conflict != NO_CONFLICT,
                "should detect at least one conflict");

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
}

/* ── main ── */
int main(void) {
    printf("=== T2: PROPAGATE (Two-Watched-Literal BCP) ===\n");
    RUN_TEST(propagate_empty_trail);
    RUN_TEST(propagate_single_unit_clause);
    RUN_TEST(propagate_binary_unit);
    RUN_TEST(propagate_conflict_detected);
    RUN_TEST(propagate_watch_swap_replacement);
    RUN_TEST(propagate_clause_already_satisfied);
    RUN_TEST(propagate_multiple_implications_chain);
    RUN_TEST(propagate_stops_at_first_conflict);
    REPORT_AND_EXIT();
}
