// ============================================================================
// Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
// Feature: SMT Module
// File: src/SMT/smt_core.c
// Description: Satisfiability Modulo Theories (SMT) DPLL(T) solver harness.
//              Coordinates propositional CDCL reasoning with pluggable
//              first-order theory solvers (EUF, LRA, LIA, Bit-Vectors).
// ============================================================================

#include "smt_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __has_include("../SAT/sat_api.h")
#include "../SAT/sat_api.h"
#elif __has_include("sat_api.h")
#include "sat_api.h"
#elif __has_include("SAT/sat_api.h")
#include "SAT/sat_api.h"
#endif

#define MAX_REGISTERED_THEORIES 16
#define MAX_THEORY_BINDINGS     256

typedef struct {
    int              bool_var;
    SodiumTheoryType theory_type;
    char*            expression;
} TheoryAtomBinding;

typedef struct {
    SodiumTheoryInterface iface;
    void*                 instance;
    bool                  is_active;
} RegisteredTheory;

struct SodiumSMTContext {
    RegisteredTheory  theories[MAX_REGISTERED_THEORIES];
    size_t            theory_count;

    TheoryAtomBinding bindings[MAX_THEORY_BINDINGS];
    size_t            binding_count;

    // Internal clause storage
    int**             clauses;
    size_t*           clause_sizes;
    size_t            clause_count;
    size_t            clause_capacity;

    int               max_var;
    int               last_status;
};

SodiumSMTContext* sodium_smt_create(void) {
    SodiumSMTContext* ctx = (SodiumSMTContext*)calloc(1, sizeof(SodiumSMTContext));
    if (!ctx) return NULL;

    ctx->clause_capacity = 64;
    ctx->clauses = (int**)malloc(ctx->clause_capacity * sizeof(int*));
    ctx->clause_sizes = (size_t*)malloc(ctx->clause_capacity * sizeof(size_t));
    ctx->last_status = SODIUM_SMT_UNKNOWN;
    return ctx;
}

void sodium_smt_destroy(SodiumSMTContext* ctx) {
    if (!ctx) return;

    // Release theory instances
    for (size_t i = 0; i < ctx->theory_count; i++) {
        if (ctx->theories[i].is_active && ctx->theories[i].iface.destroy_instance) {
            ctx->theories[i].iface.destroy_instance(ctx->theories[i].instance);
        }
    }

    // Release theory atom bindings
    for (size_t i = 0; i < ctx->binding_count; i++) {
        if (ctx->bindings[i].expression) {
            free(ctx->bindings[i].expression);
        }
    }

    // Release clauses
    for (size_t i = 0; i < ctx->clause_count; i++) {
        if (ctx->clauses[i]) {
            free(ctx->clauses[i]);
        }
    }
    if (ctx->clauses) free(ctx->clauses);
    if (ctx->clause_sizes) free(ctx->clause_sizes);

    free(ctx);
}

int sodium_smt_register_theory(SodiumSMTContext* ctx, const SodiumTheoryInterface* theory) {
    if (!ctx || !theory) return -1;
    if (ctx->theory_count >= MAX_REGISTERED_THEORIES) return -2;

    RegisteredTheory* reg = &ctx->theories[ctx->theory_count];
    reg->iface = *theory;
    reg->instance = theory->create_instance ? theory->create_instance() : NULL;
    reg->is_active = true;
    ctx->theory_count++;
    return 0;
}

int sodium_smt_bind_theory_atom(SodiumSMTContext* ctx, int bool_var, SodiumTheoryType theory, const char* expr) {
    if (!ctx || bool_var <= 0 || !expr) return -1;
    if (ctx->binding_count >= MAX_THEORY_BINDINGS) return -2;

    TheoryAtomBinding* b = &ctx->bindings[ctx->binding_count++];
    b->bool_var = bool_var;
    b->theory_type = theory;
    b->expression = strdup(expr);

    if (bool_var > ctx->max_var) {
        ctx->max_var = bool_var;
    }
    return 0;
}

void sodium_smt_add_clause(SodiumSMTContext* ctx, const int* literals, size_t count) {
    if (!ctx || !literals || count == 0) return;

    if (ctx->clause_count >= ctx->clause_capacity) {
        ctx->clause_capacity *= 2;
        ctx->clauses = (int**)realloc(ctx->clauses, ctx->clause_capacity * sizeof(int*));
        ctx->clause_sizes = (size_t*)realloc(ctx->clause_sizes, ctx->clause_capacity * sizeof(size_t));
    }

    int* c = (int*)malloc(count * sizeof(int));
    memcpy(c, literals, count * sizeof(int));

    ctx->clauses[ctx->clause_count] = c;
    ctx->clause_sizes[ctx->clause_count] = count;
    ctx->clause_count++;

    for (size_t i = 0; i < count; i++) {
        int v = abs(literals[i]);
        if (v > ctx->max_var) {
            ctx->max_var = v;
        }
    }
}

int sodium_smt_check(SodiumSMTContext* ctx) {
    if (!ctx) return SODIUM_SMT_UNKNOWN;

    // Allocate in-memory SAT solver for propositional abstraction
    SodiumSolver* sat = sodium_solver_create();
    if (!sat) return SODIUM_SMT_UNKNOWN;

    // Load initial clauses into propositional solver
    for (size_t i = 0; i < ctx->clause_count; i++) {
        for (size_t j = 0; j < ctx->clause_sizes[i]; j++) {
            sodium_solver_add_literal(sat, ctx->clauses[i][j]);
        }
        sodium_solver_add_literal(sat, 0); // Terminate clause
    }

    // DPLL(T) Iteration Loop
    int result = SODIUM_SMT_UNKNOWN;
    const int MAX_DPLL_T_ITERATIONS = 500;

    for (int iter = 0; iter < MAX_DPLL_T_ITERATIONS; iter++) {
        int sat_status = sodium_solver_solve(sat);

        if (sat_status == SODIUM_STATUS_UNSATISFIABLE) {
            // Propositional core is unsatisfiable -> formula is unsatisfiable
            result = SODIUM_SMT_UNSAT;
            break;
        }

        if (sat_status != SODIUM_STATUS_SATISFIABLE) {
            result = SODIUM_SMT_UNKNOWN;
            break;
        }

        // If no theories are registered, pure propositional SAT is sufficient
        if (ctx->theory_count == 0) {
            result = SODIUM_SMT_SAT;
            break;
        }

        // Check assigned theory atoms across registered theory solvers
        bool all_theories_consistent = true;

        for (size_t t = 0; t < ctx->theory_count; t++) {
            RegisteredTheory* reg = &ctx->theories[t];
            if (!reg->is_active) continue;

            if (reg->iface.reset) {
                reg->iface.reset(reg->instance);
            }

            // Assert truth values of bound theory atoms in current propositional model
            for (size_t b = 0; b < ctx->binding_count; b++) {
                if (ctx->bindings[b].theory_type == reg->iface.theory_type) {
                    int var = ctx->bindings[b].bool_var;
                    int lit_val = sodium_solver_get_value(sat, var);
                    bool is_assigned_true = (lit_val > 0);

                    if (reg->iface.assert_literal) {
                        reg->iface.assert_literal(reg->instance, var, is_assigned_true);
                    }
                }
            }

            // Run T-Consistency check
            bool theory_ok = true;
            if (reg->iface.check_consistency) {
                theory_ok = reg->iface.check_consistency(reg->instance);
            }

            if (!theory_ok) {
                all_theories_consistent = false;

                // Extract minimal theory conflict lemma (T-lemma)
                int conflict_lits[128];
                int conflict_size = 0;
                if (reg->iface.explain_conflict) {
                    conflict_size = reg->iface.explain_conflict(reg->instance, conflict_lits, 128);
                }

                // Add learned theory conflict clause to SAT solver to eliminate this assignment
                if (conflict_size > 0) {
                    for (int k = 0; k < conflict_size; k++) {
                        sodium_solver_add_literal(sat, conflict_lits[k]);
                    }
                    sodium_solver_add_literal(sat, 0);
                }
                break; // Inconsistent; retry propositional search
            }
        }

        if (all_theories_consistent) {
            result = SODIUM_SMT_SAT;
            break;
        }
    }

    sodium_solver_destroy(sat);
    ctx->last_status = result;
    return result;
}

void sodium_smt_reset(SodiumSMTContext* ctx) {
    if (!ctx) return;

    for (size_t i = 0; i < ctx->clause_count; i++) {
        if (ctx->clauses[i]) free(ctx->clauses[i]);
    }
    ctx->clause_count = 0;

    for (size_t i = 0; i < ctx->binding_count; i++) {
        if (ctx->bindings[i].expression) free(ctx->bindings[i].expression);
    }
    ctx->binding_count = 0;

    for (size_t i = 0; i < ctx->theory_count; i++) {
        if (ctx->theories[i].is_active && ctx->theories[i].iface.reset) {
            ctx->theories[i].iface.reset(ctx->theories[i].instance);
        }
    }

    ctx->max_var = 0;
    ctx->last_status = SODIUM_SMT_UNKNOWN;
}
