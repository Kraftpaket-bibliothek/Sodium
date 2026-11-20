#ifndef CDCL_H
#define CDCL_H

#if __has_include("Clauses/clause_array.h")
#include "Clauses/clause_array.h"
#elif __has_include("clause_array.h")
#include "clause_array.h"
#else
#include "src/SAT/CDCL/Clauses/clause_array.h"
#endif

#include "memory.h"
#include "VSIDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

/* Literal and Assignment Values */
#ifdef UNASSIGNED
#undef UNASSIGNED
#endif
typedef enum {
    UNASSIGNED,
    TRUE,
    FALSE
} AssignmentValue;

typedef enum {
    NORMAL_SEARCH,
    CONFLICT_ANALYSIS,
    UNSAT,
    SAT,
    READY
} SolverMode;

#ifndef NO_REASON
#define NO_REASON           CLAUSE_REF_NULL
#endif
#ifndef NO_CONFLICT
#define NO_CONFLICT         CLAUSE_REF_NULL
#endif
#ifndef UNDEFINED
#define UNDEFINED           UNASSIGNED
#endif
#ifndef VAL_TRUE
#define VAL_TRUE            TRUE
#endif
#ifndef VAL_FALSE
#define VAL_FALSE           FALSE
#endif

#ifndef SUCCESS
#define SUCCESS             0
#endif
#ifndef FAILURE
#define FAILURE            -1
#endif
#ifndef ERROR
#define ERROR              -1
#endif
#ifndef CONTINUE
#define CONTINUE            1
#endif
#ifndef ANALYSIS_ERROR
#define ANALYSIS_ERROR     -1
#endif

/* Helper macros for signed literal representation */
#define VARIABLE_OF(lit)        (abs(lit))
#define COMPLEMENT(lit)         (-(lit))
#ifndef LIT_TO_INDEX
#define LIT_TO_INDEX(lit, n)    ((lit) > 0 ? (size_t)(lit) : (size_t)((n) + abs(lit)))
#endif

typedef struct Clause {
    int*    literals;           // Array of signed integers
    int     size;               // Number of literals
    bool    is_learned;         // Whether the clause was learned (or theory lemma)
    double  activity;           // Usefulness score for database reduction
    bool    is_deleted;         // Whether the clause has been removed
} Clause;

typedef struct CDCLSolver {
    int                 number_of_variables;

    /* Assignment and implication graph (1-indexed) */
    AssignmentValue*    assignment;         // Current truth assignment
    AssignmentValue*    saved_polarity;     // Phase saving memory
    int*                decision_level;     // Decision level per variable
    ClauseRef*          reason;             // Antecedent / explanation clause offset (CLAUSE_REF_NULL for decisions)

    /* Chronological assignment trail */
    int*                trail;              // Stack of assigned literals
    int                 trail_size;
    int                 trail_capacity;
    int*                trail_limits;       // Start index of each decision level
    int                 trail_limits_size;
    int                 trail_limits_capacity;
    int                 current_level;
    int                 propagation_head;   // Next trail literal to propagate

    /* Two-watched-literal (2WL) arena subsystem */
    ClauseArray         arena;              // High-performance clause arena
    WatchList*          watch_lists;        // Watch lists indexed by literal index
    ClauseRef*          clauses;            // Active original, learned, and theory clauses (references)
    int                 clause_count;
    int                 clause_capacity;

    /* Conflict analysis scratchpad */
    bool*               seen;               // Marked variables during 1-UIP traversal
    int*                bump_variables;     // Variables to bump in VSIDS
    int                 bump_variables_count;
    int                 bump_variables_capacity;
    int*                learned_literals;   // Reusable scratchpad for 1-UIP construction
    int                 learned_literals_capacity;
    SolverMode          solver_mode;

    /* Heuristics and Memory */
    MemoryManager       memory_manager;
    VSIDS               vsids;

    /* Restart and database maintenance */
    int                 conflict_count;
    int                 conflicts_since_restart;
    int                 restart_limit;
    int                 restart_base;
    int                 restart_index;
    int                 learned_clause_count;
    int                 min_learned_clauses;
    int                 next_refresh_conflict;
    int                 refresh_interval;
} CDCLSolver;


/**
 * @brief Initializes the CDCL solver state, reasoning subsystems, and input clauses.
 */
SolverMode INITIALIZE_SOLVER(CDCLSolver* solver, int variables, Clause** input_clauses, int input_clause_count);

/**
 * @brief Initializes the CDCL solver state and reasoning subsystems from an already
 *        populated ClauseArray arena.
 */
SolverMode INITIALIZE_SOLVER_FROM_ARENA(CDCLSolver* solver, int variables);

/**
 * @brief Runs the CDCL search loop to completion (SAT, UNSAT, or ERROR).
 */
SolverMode SOLVE(CDCLSolver* solver, int number_of_variables, Clause** input_clauses, int input_clause_count);

/**
 * @brief Runs the CDCL search loop on clauses pre-populated in solver->arena.
 */
SolverMode SOLVE_ARENA(CDCLSolver* solver, int number_of_variables);

/**
 * @brief Performs in-place sliding compaction of the clause arena at decision level 0,
 *        sliding active clauses down, updating antecedent reasons, and completely
 *        rebuilding the Two-Watched-Literal (2-WL) watch lists.
 */
void COMPACT_CLAUSE_DATABASE(CDCLSolver* solver);

/**
 * @brief Cleans up and frees all memory allocated across the solver and its subsystems.
 */
void DESTROY_SOLVER(CDCLSolver* solver);


/**
 * @brief Enqueues an assignment on the trail at the current decision level.
 *        Used by theory solvers for theory-propagated literals (with reason).
 */
bool ENQUEUE(CDCLSolver* solver, int literal, ClauseRef reason_clause);

/**
 * @brief Propagates Boolean assignments via Two-Watched Literals.
 *        Returns the conflicting clause if a conflict is derived, otherwise NO_CONFLICT.
 */
ClauseRef PROPAGATE(CDCLSolver* solver);

/**
 * @brief Adds and watches an external or learned clause (e.g. SMT Theory Lemma / Conflict Clause).
 */
void ATTACH_CLAUSE(CDCLSolver* solver, ClauseRef ref);

/**
 * @brief Backtracks to a specific decision level, unassigning trail variables and saving polarity.
 *        Used by SMT when theory conflict occurs or during push/pop scoping.
 */
int BACKTRACK(CDCLSolver* solver, int target_level);

/**
 * @brief Derives 1-UIP learned clause and assertion backtrack level from a conflict.
 *        Also handles explanation generation for theory conflicts.
 */
int ANALYZE_CONFLICT(CDCLSolver* solver, ClauseRef conflict_clause, ClauseRef* out_learned_clause);

/**
 * @brief Resolves a conflict, learns clause, updates heuristics, and backjumps.
 */
int HANDLE_CONFLICT(CDCLSolver* solver, ClauseRef conflict_clause);

int INITIALIZE_REASONING_SUBSYSTEMS(CDCLSolver* solver, int variables);
int REFRESH_CLAUSE_DATABASE(CDCLSolver* solver);
bool IS_DATABASE_REFRESH_DUE(const CDCLSolver* solver);
int COMPUTE_RESTART_LIMIT(int base, int restart_index);
int RESTART(CDCLSolver* solver);
void UPDATE_BRANCHING_HEURISTIC(CDCLSolver* solver);
int SELECT_BRANCH_VARIABLE(CDCLSolver* solver);

#endif // CDCL_H
