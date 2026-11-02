#ifndef SODIUM_SMT_CORE_H
#define SODIUM_SMT_CORE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* SMT Theory Identifiers                                                    */
/* ========================================================================= */
typedef enum {
    SODIUM_THEORY_EUF = 1,  /* Equality and Uninterpreted Functions (Congruence Closure) */
    SODIUM_THEORY_LRA = 2,  /* Linear Real Arithmetic (Simplex) */
    SODIUM_THEORY_LIA = 3,  /* Linear Integer Arithmetic (Branch-and-Bound / Cuts) */
    SODIUM_THEORY_BV  = 4,  /* Fixed-Size Bit-Vectors */
    SODIUM_THEORY_ARR = 5   /* Arrays (Read/Write axioms) */
} SodiumTheoryType;

/* SMT Solve Status */
typedef enum {
    SODIUM_SMT_UNKNOWN = 0,
    SODIUM_SMT_SAT     = 10,
    SODIUM_SMT_UNSAT   = 20
} SodiumSMTStatus;

/* ========================================================================= */
/* Theory Solver VTable Interface (DPLL(T) Decoupling Contract)             */
/* ========================================================================= */
typedef struct {
    SodiumTheoryType theory_type;
    const char*      theory_name;

    /* Lifecycle */
    void* (*create_instance)(void);
    void  (*destroy_instance)(void* instance);

    /* DPLL(T) Callbacks */
    bool  (*assert_literal)(void* instance, int bool_var, bool is_true);
    bool  (*check_consistency)(void* instance);
    int   (*propagate_literals)(void* instance, int* out_implied_lits, int max_lits);
    int   (*explain_conflict)(void* instance, int* out_conflict_clause, int max_lits);
    void  (*push_level)(void* instance);
    void  (*pop_level)(void* instance);
    void  (*reset)(void* instance);
} SodiumTheoryInterface;

/* ========================================================================= */
/* Core SMT Engine Context                                                  */
/* ========================================================================= */
typedef struct SodiumSMTContext SodiumSMTContext;

/**
 * @brief Allocates and initializes an SMT context using Sodium's CDCL core
 *        as the underlying propositional reasoning engine.
 */
SodiumSMTContext* sodium_smt_create(void);

/**
 * @brief Destroys an SMT context and releases all registered theory solvers.
 */
void              sodium_smt_destroy(SodiumSMTContext* ctx);

/**
 * @brief Registers a theory solver with the SMT context.
 */
int               sodium_smt_register_theory(SodiumSMTContext* ctx, const SodiumTheoryInterface* theory);

/**
 * @brief Maps a boolean abstraction variable to a theory constraint expression.
 */
int               sodium_smt_bind_theory_atom(SodiumSMTContext* ctx, int bool_var, SodiumTheoryType theory, const char* expr);

/**
 * @brief Asserts a propositional clause (which may contain theory atoms).
 */
void              sodium_smt_add_clause(SodiumSMTContext* ctx, const int* literals, size_t count);

/**
 * @brief Executes the DPLL(T) decision loop to determine satisfiability modulo theories.
 *
 * @param ctx SMT context.
 * @return SODIUM_SMT_SAT (10), SODIUM_SMT_UNSAT (20), or SODIUM_SMT_UNKNOWN (0).
 */
int               sodium_smt_check(SodiumSMTContext* ctx);

/**
 * @brief Resets the SMT assertion stack while preserving registered theories.
 */
void              sodium_smt_reset(SodiumSMTContext* ctx);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_SMT_CORE_H
