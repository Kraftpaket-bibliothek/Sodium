#ifndef CDCL_H
#define CDCL_H

#include "memory.h"
#include "VSIDS.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

/* Literal and Assignment Values */
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

#define NO_REASON           NULL
#define NO_CONFLICT         NULL
#define UNDEFINED           UNASSIGNED
#define VAL_TRUE            TRUE
#define VAL_FALSE           FALSE

#define SUCCESS             0
#define FAILURE            -1
#define ERROR              -1
#define CONTINUE            1

/* Helper macros for signed literal representation */
#define VARIABLE_OF(lit)        (abs(lit))
#define COMPLEMENT(lit)         (-(lit))
#define LIT_TO_INDEX(lit, n)    ((lit) > 0 ? (size_t)(lit) : (size_t)((n) + abs(lit)))

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
    Clause**            reason;             // Antecedent / explanation clause (NULL for decisions & theory propagations)

    /* Chronological assignment trail */
    int*                trail;              // Stack of assigned literals
    int                 trail_size;
    int                 trail_capacity;
    int*                trail_limits;       // Start index of each decision level
    int                 trail_limits_size;
    int                 trail_limits_capacity;
    int                 current_level;
    int                 propagation_head;   // Next trail literal to propagate

    /* Two-watched-literal (2WL) data */
    Clause***           watch_lists;        // Watch lists indexed by literal index
    int*                watch_list_sizes;
    int*                watch_list_capacities;
    Clause**            clauses;            // Active original, learned, and theory clauses
    int                 clause_count;
    int                 clause_capacity;

    /* Conflict analysis scratchpad */
    bool*               seen;               // Marked variables during 1-UIP traversal
    int*                bump_variables;     // Variables to bump in VSIDS
    int                 bump_variables_count;
    int                 bump_variables_capacity;
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
 * @brief Runs the CDCL search loop to completion (SAT, UNSAT, or ERROR).
 */
SolverMode SOLVE(CDCLSolver* solver, int number_of_variables, Clause** input_clauses, int input_clause_count);

/**
 * @brief Cleans up and frees all memory allocated across the solver and its subsystems.
 */
void DESTROY_SOLVER(CDCLSolver* solver);


/**
 * @brief Enqueues an assignment on the trail at the current decision level.
 *        Used by theory solvers for theory-propagated literals (with reason).
 */
bool ENQUEUE(CDCLSolver* solver, int literal, Clause* reason_clause);

/**
 * @brief Propagates Boolean assignments via Two-Watched Literals.
 *        Returns the conflicting clause if a conflict is derived, otherwise NO_CONFLICT.
 */
Clause* PROPAGATE(CDCLSolver* solver);

/**
 * @brief Adds and watches an external or learned clause (e.g. SMT Theory Lemma / Conflict Clause).
 */
void ATTACH_CLAUSE(CDCLSolver* solver, Clause* clause);

/**
 * @brief Backtracks to a specific decision level, unassigning trail variables and saving polarity.
 *        Used by SMT when theory conflict occurs or during push/pop scoping.
 */
int BACKTRACK(CDCLSolver* solver, int target_level);

/**
 * @brief Derives 1-UIP learned clause and assertion backtrack level from a conflict.
 *        Also handles explanation generation for theory conflicts.
 */
int ANALYZE_CONFLICT(CDCLSolver* solver, Clause* conflict_clause, Clause** out_learned_clause);

/**
 * @brief Resolves a conflict, learns clause, updates heuristics, and backjumps.
 */
int HANDLE_CONFLICT(CDCLSolver* solver, Clause* conflict_clause);

#endif // CDCL_H
