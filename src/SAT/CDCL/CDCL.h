#ifndef CDCL_H
#define CDCL_H

#include "memory.h"
#include "VSIDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#define UNASSIGNED          0
#define VAL_TRUE            1
#define VAL_FALSE          -1

#define NO_REASON            NULL

/* Helper macro for variable and literal representation */
#define VARIABLE_OF(lit)        (abs(lit))
#define NEGATE(lit)             (-(lit))
#define LIT_TO_INDEX(lit, n)    ((lit) > 0 ? (size_t)(lit) : (size_t)((n) + abs(lit)))

typedef struct Clause {
    int*    lits;
    size_t  size;
    bool    is_learned;
} Clause;

typedef struct ClauseVector {
    Clause** data;
    size_t   size;
    size_t   capacity;
} ClauseVector;

typedef struct LitVector {
    int*    data;
    size_t  size;
    size_t  capacity;
} LitVector;

typedef struct SizeVector {
    size_t* data;
    size_t  size;
    size_t  capacity;
} SizeVector;

typedef struct CDCLSolver {
    size_t          number_of_variables;

    /* Central memory manager instance */
    MemoryManager*  memory_manager;

    /* Trail Manager State */
    int*            assignment;         /* 1-indexed: UNASSIGNED, VAL_TRUE, VAL_FALSE */
    size_t*         decision_level;     /* 1-indexed: level per variable */
    Clause**        reason;             /* 1-indexed: antecedent clause per variable */
    LitVector       trail;              /* Stack of assigned literals */
    SizeVector      trail_limits;       /* Start index of each decision level */
    size_t          current_level;
    size_t          propagation_head;

    /* Watch Lists: 2 * num_vars + 1 buckets */
    ClauseVector*   watch_lists;

    /* Clause Database */
    ClauseVector    clauses;

    /* Heuristics */
    VSIDS           vsids;

    /* Scratchpad for conflict analysis */
    bool*           seen;
    SizeVector      bump_variables;
} CDCLSolver;

/* Core Public Solver API */
void initialize_trail(CDCLSolver* solver, size_t number_of_variables);
Clause* create_clause(const int* lits, size_t size, bool is_learned);
bool solve(CDCLSolver* solver, size_t num_vars, Clause** original_clauses, size_t num_clauses);
void destroy_solver(CDCLSolver* solver);

/* Internal / Modular Methods */
void new_decision_level(CDCLSolver* solver);
bool enqueue(CDCLSolver* solver, int literal, Clause* reason_clause);
void backtrack(CDCLSolver* solver, size_t target_level);
Clause* propagate(CDCLSolver* solver);
void analyze(CDCLSolver* solver, Clause* conflict_clause, LitVector* learned_clause, size_t* backtrack_level);
int select_polarity(size_t variable);

#endif // CDCL_H
