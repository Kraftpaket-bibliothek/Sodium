/**
 * test_10m_clauses.c  ── 10 Million Clause Stress Benchmark
 * ==========================================================
 * Massive scale stress test constructing and solving a 10,000,000-clause
 * instance across 50,000 variables.
 *
 * Combinatorial diversity:
 *   - Variable clause sizes: 2-SAT, 3-SAT, 4-SAT, 5-SAT, 6-SAT
 *   - Varied polarities: all-positive, all-negative, mixed alternating
 *   - Varied variable spans: local clusters + long-range cross-dependencies
 *   - Permuted satisfying watch literal positions (0 through k-1)
 *
 * Memory management:
 *   Uses chunked memory pools (10 chunks x 1,000,000 clauses) to eliminate
 *   individual malloc overhead and guarantee cache locality.
 */

#include "test_harness.h"
#include <time.h>

#define TOTAL_CLAUSES 10000000
#define NUM_VARIABLES    50000
#define CHUNK_SIZE     1000000
#define NUM_CHUNKS          10

/* ── Fast 64-bit LCG pseudo-random number generator ── */
static uint64_t g_rng_state = 0x853c49e6748fea9bULL;

static inline uint32_t fast_rand(void) {
    g_rng_state ^= g_rng_state >> 12;
    g_rng_state ^= g_rng_state << 25;
    g_rng_state ^= g_rng_state >> 27;
    return (uint32_t)((g_rng_state * 0x2545F4914F6CDD1DULL) >> 32);
}

/* ── Chunked Memory Pools ── */
typedef struct {
    Clause* clause_pool;
    int*    literal_pool;
} ClauseChunk;

static ClauseChunk g_chunks[NUM_CHUNKS];
static Clause**    g_all_clauses = NULL;

static void allocate_and_generate_10m_clauses(void) {
    printf("[1/4] Generating 10,000,000 clauses with diverse combinations...\n");
    clock_t start = clock();

    g_all_clauses = (Clause**)malloc((size_t)TOTAL_CLAUSES * sizeof(Clause*));
    if (!g_all_clauses) {
        fprintf(stderr, "Failed to allocate clause pointer array\n");
        exit(1);
    }

    size_t clause_global_idx = 0;

    for (int chunk_idx = 0; chunk_idx < NUM_CHUNKS; chunk_idx++) {
        /* Allocate chunk of 1,000,000 Clause structs (32 MB) */
        g_chunks[chunk_idx].clause_pool = (Clause*)malloc((size_t)CHUNK_SIZE * sizeof(Clause));
        /* Allocate chunk of literals (max 6 literals per clause = 24 MB) */
        g_chunks[chunk_idx].literal_pool = (int*)malloc((size_t)CHUNK_SIZE * 6 * sizeof(int));

        if (!g_chunks[chunk_idx].clause_pool || !g_chunks[chunk_idx].literal_pool) {
            fprintf(stderr, "Failed to allocate memory for chunk %d\n", chunk_idx);
            exit(1);
        }

        Clause* c_pool = g_chunks[chunk_idx].clause_pool;
        int*    l_pool = g_chunks[chunk_idx].literal_pool;
        size_t  l_offset = 0;

        for (int local_i = 0; local_i < CHUNK_SIZE; local_i++) {
            size_t i = clause_global_idx++;

            /* 1. Vary clause size: 2, 3, 4, 5, or 6 literals */
            int k = (int)(i % 5) + 2;

            /* 2. Choose satisfying variable for planted assignment:
             *    Planted assignment: variable v is TRUE if (v % 2 == 1), FALSE if (v % 2 == 0)
             */
            int v0 = (int)((i * 17 + 1) % NUM_VARIABLES) + 1;
            int sat_lit = (v0 % 2 == 1) ? v0 : -v0;

            int* c_lits = &l_pool[l_offset];
            l_offset += (size_t)k;

            c_lits[0] = sat_lit;

            /* 3. Fill remaining (k-1) literals with diverse combinations */
            for (int j = 1; j < k; j++) {
                uint32_t r = fast_rand();
                /* Mix of localized offsets and long-range cross links */
                int stride = (j % 2 == 1) ? (int)(j * 7919) : (int)(r % 1000 + 1);
                int vj = ((v0 + stride) % NUM_VARIABLES) + 1;

                /* Ensure distinct variables inside the clause */
                for (int prev = 0; prev < j; prev++) {
                    if (abs(c_lits[prev]) == vj) {
                        vj = (vj % NUM_VARIABLES) + 1;
                        prev = -1;
                    }
                }

                /* Varied polarities: positive, negative, or alternating */
                int sign = (r & 1) ? 1 : -1;
                c_lits[j] = sign * vj;
            }

            /* 4. Permute the satisfying literal to varied watch positions */
            int swap_pos = (int)(i % (size_t)k);
            if (swap_pos != 0) {
                int tmp = c_lits[0];
                c_lits[0] = c_lits[swap_pos];
                c_lits[swap_pos] = tmp;
            }

            /* Initialize Clause struct */
            c_pool[local_i].literals   = c_lits;
            c_pool[local_i].size       = k;
            c_pool[local_i].is_learned = false;
            c_pool[local_i].activity   = 0.0;
            c_pool[local_i].is_deleted = false;

            g_all_clauses[i] = &c_pool[local_i];
        }

        printf("      Generated %d,000,000 / %d clauses...\n", chunk_idx + 1, TOTAL_CLAUSES / 1000000);
    }

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("      Generation completed in %.2f seconds.\n", elapsed);
}

static void free_10m_clauses(void) {
    for (int chunk_idx = 0; chunk_idx < NUM_CHUNKS; chunk_idx++) {
        free(g_chunks[chunk_idx].clause_pool);
        free(g_chunks[chunk_idx].literal_pool);
        g_chunks[chunk_idx].clause_pool = NULL;
        g_chunks[chunk_idx].literal_pool = NULL;
    }
    free(g_all_clauses);
    g_all_clauses = NULL;
}

/* ── Benchmark Execution ── */
static void run_10m_clause_test(void) {
    allocate_and_generate_10m_clauses();

    printf("[2/4] Initializing CDCL Solver with 10,000,000 clauses across %d variables...\n", NUM_VARIABLES);
    clock_t start_solve = clock();

    CDCLSolver solver;
    SolverMode mode = SOLVE(&solver, NUM_VARIABLES, g_all_clauses, TOTAL_CLAUSES);

    double solve_time = (double)(clock() - start_solve) / CLOCKS_PER_SEC;

    printf("[3/4] Solver finished in %.2f seconds.\n", solve_time);
    printf("      Solver Result: %s\n", (mode == SAT) ? "SATISFIABLE" : ((mode == UNSAT) ? "UNSATISFIABLE" : "ERROR"));

    TEST_ASSERT(mode == SAT, "10-Million clause instance must be SAT");

    printf("[4/4] Verifying model across sample clauses...\n");
    /* Sample verify 100,000 clauses across all parts of the database */
    int verified = 0;
    for (size_t i = 0; i < TOTAL_CLAUSES; i += 100) {
        TEST_ASSERT(check_clause_satisfied(&solver, g_all_clauses[i]),
                    "clause in 10M set must be satisfied by model");
        verified++;
    }
    printf("      Verified %d sample clauses: all satisfied!\n", verified);

    printf("Cleaning up solver and memory pools...\n");
    DESTROY_SOLVER(&solver);
    free_10m_clauses();
    printf("Cleanup complete.\n");
}

int main(void) {
    printf("=== 10 Million Clause Stress Test ===\n");
    RUN_TEST(run_10m_clause_test);
    REPORT_AND_EXIT();
}
