/**
 * test_benchmarks.c  ── Large Benchmark Instances
 * =================================================
 * Exercises Sodium's CDCL solver on larger combinatorial benchmarks:
 *   1. N-Queens Problem:
 *      - 4-Queens  (16 vars, 84 clauses)  → SAT (solution verified in domain)
 *      - 8-Queens  (64 vars, 744 clauses) → SAT (solution verified in domain)
 *      - 2-Queens  (4 vars, 10 clauses)   → UNSAT (provably no solution)
 *      - 3-Queens  (9 vars, 36 clauses)   → UNSAT (provably no solution)
 *   2. Pigeonhole Principle (PHP):
 *      - PHP(4->3) (12 vars, 22 clauses)  → UNSAT (deep 1-UIP conflict search)
 *      - PHP(5->4) (20 vars, 45 clauses)  → UNSAT (multi-level backjumping)
 *   3. 4x4 Mini-Sudoku:
 *      - Solvable puzzle with clues       → SAT (completeness & validity)
 *      - Over-constrained / contradictory clues → UNSAT
 */

#include "test_harness.h"
#include <math.h>

/* ========================================================================= */
/* Dynamic Clause Collector Helper                                            */
/* ========================================================================= */

typedef struct {
    Clause** clauses;
    int      count;
    int      capacity;
} ClauseCollector;

static void collector_init(ClauseCollector* cc) {
    cc->count = 0;
    cc->capacity = 64;
    cc->clauses = (Clause**)malloc((size_t)cc->capacity * sizeof(Clause*));
}

static void collector_add(ClauseCollector* cc, const int* lits, int n) {
    if (cc->count >= cc->capacity) {
        cc->capacity *= 2;
        cc->clauses = (Clause**)realloc(cc->clauses, (size_t)cc->capacity * sizeof(Clause*));
    }
    cc->clauses[cc->count++] = make_clause(lits, n, false);
}

static void collector_free(ClauseCollector* cc) {
    for (int i = 0; i < cc->count; i++) {
        free_clause(cc->clauses[i]);
    }
    free(cc->clauses);
    cc->clauses = NULL;
    cc->count = 0;
    cc->capacity = 0;
}

/* ========================================================================= */
/* 1. N-Queens CNF Generator & Validator                                     */
/* ========================================================================= */

/**
 * Variable indexing: queen at (row, col) where 0 <= row, col < n
 * var_id = row * n + col + 1  (1 .. n^2)
 */
static inline int queen_var(int r, int c, int n) {
    return r * n + c + 1;
}

static void generate_nqueens_cnf(int n, ClauseCollector* cc) {
    collector_init(cc);

    /* 1. Each row has at least one queen: \/_{c=0}^{n-1} x(r, c) */
    int* row_lits = (int*)malloc((size_t)n * sizeof(int));
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            row_lits[c] = queen_var(r, c, n);
        }
        collector_add(cc, row_lits, n);
    }
    free(row_lits);

    /* 2. Each row has at most one queen: -x(r, c1) \/ -x(r, c2) */
    for (int r = 0; r < n; r++) {
        for (int c1 = 0; c1 < n; c1++) {
            for (int c2 = c1 + 1; c2 < n; c2++) {
                int pair[2] = { -queen_var(r, c1, n), -queen_var(r, c2, n) };
                collector_add(cc, pair, 2);
            }
        }
    }

    /* 3. Each column has at most one queen: -x(r1, c) \/ -x(r2, c) */
    for (int c = 0; c < n; c++) {
        for (int r1 = 0; r1 < n; r1++) {
            for (int r2 = r1 + 1; r2 < n; r2++) {
                int pair[2] = { -queen_var(r1, c, n), -queen_var(r2, c, n) };
                collector_add(cc, pair, 2);
            }
        }
    }

    /* 4. Diagonals have at most one queen:
     *    For all pairs (r1, c1) and (r2, c2) with r1 < r2:
     *    if |r1 - r2| == |c1 - c2|: -x(r1, c1) \/ -x(r2, c2)
     */
    for (int r1 = 0; r1 < n; r1++) {
        for (int c1 = 0; c1 < n; c1++) {
            for (int r2 = r1 + 1; r2 < n; r2++) {
                for (int c2 = 0; c2 < n; c2++) {
                    if (abs(r1 - r2) == abs(c1 - c2)) {
                        int pair[2] = { -queen_var(r1, c1, n), -queen_var(r2, c2, n) };
                        collector_add(cc, pair, 2);
                    }
                }
            }
        }
    }
}

/**
 * Domain-level validation: checks whether the solver assignment is an
 * authentic N-Queens solution.
 */
static bool verify_nqueens_solution(const CDCLSolver* solver, int n) {
    int total_queens = 0;
    int* queen_cols = (int*)malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) queen_cols[i] = -1;

    for (int r = 0; r < n; r++) {
        int queens_in_row = 0;
        for (int c = 0; c < n; c++) {
            int v = queen_var(r, c, n);
            if (solver->assignment[v] == TRUE) {
                queens_in_row++;
                queen_cols[r] = c;
                total_queens++;
            }
        }
        if (queens_in_row != 1) {
            free(queen_cols);
            return false;
        }
    }

    if (total_queens != n) {
        free(queen_cols);
        return false;
    }

    /* Verify no column or diagonal conflicts */
    for (int r1 = 0; r1 < n; r1++) {
        for (int r2 = r1 + 1; r2 < n; r2++) {
            int c1 = queen_cols[r1];
            int c2 = queen_cols[r2];
            if (c1 == c2) { free(queen_cols); return false; }
            if (abs(r1 - r2) == abs(c1 - c2)) { free(queen_cols); return false; }
        }
    }

    free(queen_cols);
    return true;
}

/* ========================================================================= */
/* 2. Pigeonhole Principle (PHP) CNF Generator                               */
/* ========================================================================= */

/**
 * Variable indexing: pigeon p in hole h (0 <= p < pigeons, 0 <= h < holes)
 * var_id = p * holes + h + 1  (1 .. pigeons * holes)
 */
static inline int php_var(int p, int h, int holes) {
    return p * holes + h + 1;
}

static void generate_php_cnf(int pigeons, int holes, ClauseCollector* cc) {
    collector_init(cc);

    /* 1. Each pigeon must be in at least one hole: \/_{h=0}^{holes-1} x(p, h) */
    int* p_lits = (int*)malloc((size_t)holes * sizeof(int));
    for (int p = 0; p < pigeons; p++) {
        for (int h = 0; h < holes; h++) {
            p_lits[h] = php_var(p, h, holes);
        }
        collector_add(cc, p_lits, holes);
    }
    free(p_lits);

    /* 2. No two pigeons in the same hole: -x(p1, h) \/ -x(p2, h) */
    for (int h = 0; h < holes; h++) {
        for (int p1 = 0; p1 < pigeons; p1++) {
            for (int p2 = p1 + 1; p2 < pigeons; p2++) {
                int pair[2] = { -php_var(p1, h, holes), -php_var(p2, h, holes) };
                collector_add(cc, pair, 2);
            }
        }
    }
}

/* ========================================================================= */
/* 3. 4x4 Mini-Sudoku CNF Generator                                          */
/* ========================================================================= */

/**
 * Variable indexing: cell (r, c) contains digit v (0 <= r, c < 4, 1 <= v <= 4)
 * var_id = (r * 4 + c) * 4 + v  (1 .. 64)
 */
static inline int sudoku_var(int r, int c, int v) {
    return (r * 4 + c) * 4 + v;
}

static void generate_sudoku_cnf(const int clues[4][4], ClauseCollector* cc) {
    collector_init(cc);

    /* 1. Each cell has at least one value */
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int cell_lits[4];
            for (int v = 1; v <= 4; v++) cell_lits[v - 1] = sudoku_var(r, c, v);
            collector_add(cc, cell_lits, 4);
        }
    }

    /* 2. Each cell has at most one value */
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            for (int v1 = 1; v1 <= 4; v1++) {
                for (int v2 = v1 + 1; v2 <= 4; v2++) {
                    int pair[2] = { -sudoku_var(r, c, v1), -sudoku_var(r, c, v2) };
                    collector_add(cc, pair, 2);
                }
            }
        }
    }

    /* 3. Each row has every digit */
    for (int r = 0; r < 4; r++) {
        for (int v = 1; v <= 4; v++) {
            int row_lits[4];
            for (int c = 0; c < 4; c++) row_lits[c] = sudoku_var(r, c, v);
            collector_add(cc, row_lits, 4);
        }
    }

    /* 4. Each column has every digit */
    for (int c = 0; c < 4; c++) {
        for (int v = 1; v <= 4; v++) {
            int col_lits[4];
            for (int r = 0; r < 4; r++) col_lits[r] = sudoku_var(r, c, v);
            collector_add(cc, col_lits, 4);
        }
    }

    /* 5. Each 2x2 box has every digit */
    for (int br = 0; br < 2; br++) {
        for (int bc = 0; bc < 2; bc++) {
            for (int v = 1; v <= 4; v++) {
                int box_lits[4];
                int idx = 0;
                for (int dr = 0; dr < 2; dr++) {
                    for (int dc = 0; dc < 2; dc++) {
                        box_lits[idx++] = sudoku_var(br * 2 + dr, bc * 2 + dc, v);
                    }
                }
                collector_add(cc, box_lits, 4);
            }
        }
    }

    /* 6. Initial clues (unit clauses) */
    if (clues) {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (clues[r][c] >= 1 && clues[r][c] <= 4) {
                    int unit[1] = { sudoku_var(r, c, clues[r][c]) };
                    collector_add(cc, unit, 1);
                }
            }
        }
    }
}

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

/* ── Benchmark 1: 4-Queens (SAT) ── */
static void benchmark_4queens_sat(void) {
    ClauseCollector cc;
    int n = 4;
    generate_nqueens_cnf(n, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, n * n, cc.clauses, cc.count);

    TEST_ASSERT(mode == SAT, "4-Queens must be SAT");

    /* Verify CNF clause satisfaction */
    for (int i = 0; i < cc.count; i++) {
        TEST_ASSERT(check_clause_satisfied(&solver, cc.clauses[i]),
                    "every 4-Queens clause must be satisfied");
    }

    /* Verify domain semantics */
    TEST_ASSERT(verify_nqueens_solution(&solver, n),
                "solution must be a valid 4-Queens configuration");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ── Benchmark 2: 8-Queens (SAT) ── */
static void benchmark_8queens_sat(void) {
    ClauseCollector cc;
    int n = 8;
    generate_nqueens_cnf(n, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, n * n, cc.clauses, cc.count);

    TEST_ASSERT(mode == SAT, "8-Queens must be SAT");

    /* Verify CNF satisfaction */
    for (int i = 0; i < cc.count; i++) {
        TEST_ASSERT(check_clause_satisfied(&solver, cc.clauses[i]),
                    "every 8-Queens clause must be satisfied");
    }

    /* Verify domain semantics */
    TEST_ASSERT(verify_nqueens_solution(&solver, n),
                "solution must be a valid 8-Queens configuration");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ── Benchmark 3: 2-Queens (UNSAT) ── */
static void benchmark_2queens_unsat(void) {
    ClauseCollector cc;
    int n = 2;
    generate_nqueens_cnf(n, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, n * n, cc.clauses, cc.count);

    TEST_ASSERT(mode == UNSAT, "2-Queens must be UNSAT");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ── Benchmark 4: 3-Queens (UNSAT) ── */
static void benchmark_3queens_unsat(void) {
    ClauseCollector cc;
    int n = 3;
    generate_nqueens_cnf(n, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, n * n, cc.clauses, cc.count);

    TEST_ASSERT(mode == UNSAT, "3-Queens must be UNSAT");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ── Benchmark 5: PHP(4 -> 3) (UNSAT) ── */
static void benchmark_php_4_into_3_unsat(void) {
    ClauseCollector cc;
    int pigeons = 4, holes = 3;
    generate_php_cnf(pigeons, holes, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, pigeons * holes, cc.clauses, cc.count);

    TEST_ASSERT(mode == UNSAT, "PHP(4->3) must be UNSAT");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ── Benchmark 6: PHP(5 -> 4) (UNSAT) ── */
static void benchmark_php_5_into_4_unsat(void) {
    ClauseCollector cc;
    int pigeons = 5, holes = 4;
    generate_php_cnf(pigeons, holes, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, pigeons * holes, cc.clauses, cc.count);

    TEST_ASSERT(mode == UNSAT, "PHP(5->4) must be UNSAT");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ── Benchmark 7: 4x4 Sudoku Valid Clues (SAT) ── */
static void benchmark_sudoku_valid_sat(void) {
    /*
     * Puzzle:
     *   [ 1, 0 | 0, 4 ]
     *   [ 0, 0 | 1, 0 ]
     *   ---------------+
     *   [ 0, 1 | 0, 0 ]
     *   [ 4, 0 | 0, 2 ]
     */
    int clues[4][4] = {
        {1, 0, 0, 4},
        {0, 0, 1, 0},
        {0, 1, 0, 0},
        {4, 0, 0, 2}
    };

    ClauseCollector cc;
    generate_sudoku_cnf(clues, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 64, cc.clauses, cc.count);

    TEST_ASSERT(mode == SAT, "4x4 Sudoku with valid clues must be SAT");

    for (int i = 0; i < cc.count; i++) {
        TEST_ASSERT(check_clause_satisfied(&solver, cc.clauses[i]),
                    "every Sudoku clause must be satisfied");
    }

    /* Verify clues are satisfied in model */
    TEST_ASSERT(solver.assignment[sudoku_var(0, 0, 1)] == TRUE, "cell (0,0) must be 1");
    TEST_ASSERT(solver.assignment[sudoku_var(0, 3, 4)] == TRUE, "cell (0,3) must be 4");
    TEST_ASSERT(solver.assignment[sudoku_var(1, 2, 1)] == TRUE, "cell (1,2) must be 1");
    TEST_ASSERT(solver.assignment[sudoku_var(2, 1, 1)] == TRUE, "cell (2,1) must be 1");
    TEST_ASSERT(solver.assignment[sudoku_var(3, 0, 4)] == TRUE, "cell (3,0) must be 4");
    TEST_ASSERT(solver.assignment[sudoku_var(3, 3, 2)] == TRUE, "cell (3,3) must be 2");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ── Benchmark 8: 4x4 Sudoku Contradictory Clues (UNSAT) ── */
static void benchmark_sudoku_contradictory_unsat(void) {
    /* Two identical digits in same row: cell (0,0)=1 and cell (0,1)=1 */
    int clues[4][4] = {
        {1, 1, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}
    };

    ClauseCollector cc;
    generate_sudoku_cnf(clues, &cc);

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, 64, cc.clauses, cc.count);

    TEST_ASSERT(mode == UNSAT, "Sudoku with row conflict clues must be UNSAT");

    DESTROY_SOLVER(&solver);
    collector_free(&cc);
}

/* ========================================================================= */
/* Main Runner                                                               */
/* ========================================================================= */

int main(void) {
    printf("=== Large Benchmark Instances ===\n");
    RUN_TEST(benchmark_4queens_sat);
    RUN_TEST(benchmark_8queens_sat);
    RUN_TEST(benchmark_2queens_unsat);
    RUN_TEST(benchmark_3queens_unsat);
    RUN_TEST(benchmark_php_4_into_3_unsat);
    RUN_TEST(benchmark_php_5_into_4_unsat);
    RUN_TEST(benchmark_sudoku_valid_sat);
    RUN_TEST(benchmark_sudoku_contradictory_unsat);
    REPORT_AND_EXIT();
}
