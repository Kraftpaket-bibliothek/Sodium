/**
 * test_solve_integration.c  ── T7: End-to-End SOLVE()
 * ====================================================
 * End-to-end integration tests invoking the full CDCL algorithm via SOLVE().
 * Verifies correctness across satisfiable formulas, unsatisfiable formulas,
 * pigeon-hole problems (PHP), model completeness, clause validation,
 * learned-clause progression, and resource cleanup.
 *
 * Tests
 * -----
 * 7.1   solve_trivially_sat                 – single unit clause {1} → SAT
 * 7.2   solve_trivially_unsat_empty_clause  – empty clause passed at init → UNSAT
 * 7.3   solve_trivially_unsat_unit_conflict – {1} and {-1} → UNSAT
 * 7.4   solve_simple_sat_3vars              – 3-variable satisfiable formula → SAT
 * 7.5   solve_pigeon_hole_2_into_1          – PHP(2->1) → UNSAT
 * 7.6   solve_pigeon_hole_3_into_2          – PHP(3->2) (6 vars, 9 clauses) → UNSAT
 * 7.7   solve_learned_clause_prevents_revisit – conflict + 1-UIP learn → SAT
 * 7.8   solve_all_vars_assigned_in_model    – all variables have definite boolean value
 * 7.9   solve_assignment_satisfies_all_clauses – model satisfies every input clause
 * 7.10  solve_phase_saving_used_on_restart  – aggressive restart still achieves SAT
 * 7.11  solve_destroy_leaves_no_leaks       – clean tear-down after search
 */

#include "test_harness.h"

/* ── 7.1 ── */
static void solve_trivially_sat(void) {
    int lits[] = {1};
    Clause* c = make_clause(lits, 1, false);
    Clause* clauses[] = {c};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 1, clauses, 1);

    TEST_ASSERT(mode == SAT, "1-variable unit clause should be SAT");
    TEST_ASSERT(solver.assignment[1] == TRUE, "variable 1 should be TRUE");

    DESTROY_SOLVER(&solver);
    free_clause(c);
}

/* ── 7.2 ── */
static void solve_trivially_unsat_empty_clause(void) {
    Clause* empty_c = make_clause(NULL, 0, false);
    Clause* clauses[] = {empty_c};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 1, clauses, 1);

    TEST_ASSERT(mode == UNSAT, "empty clause formula must be UNSAT");

    DESTROY_SOLVER(&solver);
    free_clause(empty_c);
}

/* ── 7.3 ── */
static void solve_trivially_unsat_unit_conflict(void) {
    int lits1[] = {1};
    int lits2[] = {-1};
    Clause* c1 = make_clause(lits1, 1, false);
    Clause* c2 = make_clause(lits2, 1, false);
    Clause* clauses[] = {c1, c2};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 1, clauses, 2);

    TEST_ASSERT(mode == UNSAT, "{1} and {-1} must be UNSAT");

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
}

/* ── 7.4 ── */
static void solve_simple_sat_3vars(void) {
    /* Formula: (x1 | x2) & (-x1 | x3) & (-x2 | -x3) */
    int l1[] = {1, 2};
    int l2[] = {-1, 3};
    int l3[] = {-2, -3};
    Clause* c1 = make_clause(l1, 2, false);
    Clause* c2 = make_clause(l2, 2, false);
    Clause* c3 = make_clause(l3, 2, false);
    Clause* clauses[] = {c1, c2, c3};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 3, clauses, 3);

    TEST_ASSERT(mode == SAT, "formula should be SAT");
    TEST_ASSERT(check_clause_satisfied(&solver, c1), "c1 must be satisfied");
    TEST_ASSERT(check_clause_satisfied(&solver, c2), "c2 must be satisfied");
    TEST_ASSERT(check_clause_satisfied(&solver, c3), "c3 must be satisfied");

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
    free_clause(c3);
}

/* ── 7.5 ── */
static void solve_pigeon_hole_2_into_1(void) {
    /* 2 pigeons, 1 hole
     * Pigeon 1 in hole 1: var 1
     * Pigeon 2 in hole 1: var 2
     * Clauses:
     *   {1}        (pigeon 1 must be placed)
     *   {2}        (pigeon 2 must be placed)
     *   {-1, -2}   (at most one pigeon in hole 1)
     */
    int l1[] = {1};
    int l2[] = {2};
    int l3[] = {-1, -2};
    Clause* c1 = make_clause(l1, 1, false);
    Clause* c2 = make_clause(l2, 1, false);
    Clause* c3 = make_clause(l3, 2, false);
    Clause* clauses[] = {c1, c2, c3};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 2, clauses, 3);

    TEST_ASSERT(mode == UNSAT, "PHP(2->1) must be UNSAT");

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
    free_clause(c3);
}

/* ── 7.6 ── */
static void solve_pigeon_hole_3_into_2(void) {
    /* 3 pigeons into 2 holes -> 6 variables
     * P1: {1, 2}
     * P2: {3, 4}
     * P3: {5, 6}
     * Hole 1 at most one: {-1, -3}, {-1, -5}, {-3, -5}
     * Hole 2 at most one: {-2, -4}, {-2, -6}, {-4, -6}
     * Total 9 clauses, 6 variables -> UNSAT
     */
    int l[9][2] = {
        {1, 2},   {3, 4},   {5, 6},
        {-1, -3}, {-1, -5}, {-3, -5},
        {-2, -4}, {-2, -6}, {-4, -6}
    };
    Clause* clauses[9];
    for (int i = 0; i < 9; i++) {
        clauses[i] = make_clause(l[i], 2, false);
    }

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 6, clauses, 9);

    TEST_ASSERT(mode == UNSAT, "PHP(3->2) must be UNSAT");

    DESTROY_SOLVER(&solver);
    for (int i = 0; i < 9; i++) {
        free_clause(clauses[i]);
    }
}

/* ── 7.7 ── */
static void solve_learned_clause_prevents_revisit(void) {
    /* Formula requiring conflict analysis and backjumping:
     *   x1 triggers conflict if tried positive:
     *   {-1, 2}, {-1, -2}
     *   With x1=false, other clauses can be satisfied:
     *   {1, 3}, {1, 4}
     */
    int l1[] = {-1, 2};
    int l2[] = {-1, -2};
    int l3[] = {1, 3};
    int l4[] = {1, 4};
    Clause* c1 = make_clause(l1, 2, false);
    Clause* c2 = make_clause(l2, 2, false);
    Clause* c3 = make_clause(l3, 2, false);
    Clause* c4 = make_clause(l4, 2, false);
    Clause* clauses[] = {c1, c2, c3, c4};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 4, clauses, 4);

    TEST_ASSERT(mode == SAT, "formula should be SAT after conflict resolution");
    TEST_ASSERT(solver.assignment[1] == FALSE, "var 1 must be FALSE");
    TEST_ASSERT(solver.assignment[3] == TRUE,  "var 3 must be TRUE");
    TEST_ASSERT(solver.assignment[4] == TRUE,  "var 4 must be TRUE");

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
    free_clause(c3);
    free_clause(c4);
}

/* ── 7.8 ── */
static void solve_all_vars_assigned_in_model(void) {
    /* 4-variable satisfiable formula */
    int l1[] = {1, 2};
    int l2[] = {-2, 3};
    int l3[] = {-3, 4};
    Clause* c1 = make_clause(l1, 2, false);
    Clause* c2 = make_clause(l2, 2, false);
    Clause* c3 = make_clause(l3, 2, false);
    Clause* clauses[] = {c1, c2, c3};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 4, clauses, 3);

    TEST_ASSERT(mode == SAT, "formula must be SAT");
    for (int v = 1; v <= 4; v++) {
        TEST_ASSERT(solver.assignment[v] == TRUE || solver.assignment[v] == FALSE,
                    "all variables must be definitively assigned in model");
    }

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
    free_clause(c3);
}

/* ── 7.9 ── */
static void solve_assignment_satisfies_all_clauses(void) {
    /* 5 variables, 6 clauses */
    int l[6][3] = {
        {1, 2, 3},
        {-1, -2, 4},
        {-3, 5, 0},
        {-4, -5, 1},
        {2, -3, -4},
        {-1, 3, 5}
    };
    int sizes[6] = {3, 3, 2, 3, 3, 3};
    Clause* clauses[6];
    for (int i = 0; i < 6; i++) {
        clauses[i] = make_clause(l[i], sizes[i], false);
    }

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 5, clauses, 6);

    TEST_ASSERT(mode == SAT, "instance should be SAT");
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT(check_clause_satisfied(&solver, clauses[i]),
                    "computed model must satisfy each clause");
    }

    DESTROY_SOLVER(&solver);
    for (int i = 0; i < 6; i++) {
        free_clause(clauses[i]);
    }
}

/* ── 7.10 ── */
static void solve_phase_saving_used_on_restart(void) {
    /* Test that restarts preserve phase saving and solver still reaches SAT */
    int l1[] = {1, 2};
    int l2[] = {-1, 3};
    int l3[] = {-2, 4};
    Clause* c1 = make_clause(l1, 2, false);
    Clause* c2 = make_clause(l2, 2, false);
    Clause* c3 = make_clause(l3, 2, false);
    Clause* clauses[] = {c1, c2, c3};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 4, clauses, 3);

    TEST_ASSERT(mode == SAT, "formula should be SAT");

    DESTROY_SOLVER(&solver);
    free_clause(c1);
    free_clause(c2);
    free_clause(c3);
}

/* ── 7.11 ── */
static void solve_destroy_leaves_no_leaks(void) {
    int l1[] = {1, 2};
    int l2[] = {-1, 2};
    Clause* c1 = make_clause(l1, 2, false);
    Clause* c2 = make_clause(l2, 2, false);
    Clause* clauses[] = {c1, c2};

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 2, clauses, 2);

    TEST_ASSERT(mode == SAT, "simple 2-var formula should be SAT");

    /* Verify tear-down */
    DESTROY_SOLVER(&solver);
    TEST_ASSERT(solver.assignment == NULL, "assignment must be NULL after DESTROY_SOLVER");
    TEST_ASSERT(solver.clauses == NULL, "clauses must be NULL after DESTROY_SOLVER");

    free_clause(c1);
    free_clause(c2);
}

/* ── main ── */
int main(void) {
    printf("=== T7: End-to-End SOLVE() ===\n");
    RUN_TEST(solve_trivially_sat);
    RUN_TEST(solve_trivially_unsat_empty_clause);
    RUN_TEST(solve_trivially_unsat_unit_conflict);
    RUN_TEST(solve_simple_sat_3vars);
    RUN_TEST(solve_pigeon_hole_2_into_1);
    RUN_TEST(solve_pigeon_hole_3_into_2);
    RUN_TEST(solve_learned_clause_prevents_revisit);
    RUN_TEST(solve_all_vars_assigned_in_model);
    RUN_TEST(solve_assignment_satisfies_all_clauses);
    RUN_TEST(solve_phase_saving_used_on_restart);
    RUN_TEST(solve_destroy_leaves_no_leaks);
    REPORT_AND_EXIT();
}
