#ifndef SODIUM_API_H
#define SODIUM_API_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* CaDiCaL-Referenced Status Codes                                           */
/* ========================================================================= */
#define SODIUM_STATUS_UNKNOWN        0
#define SODIUM_STATUS_SATISFIABLE   10
#define SODIUM_STATUS_UNSATISFIABLE 20

/* ========================================================================= */
/* 1. High-Level JSON Pipeline API                                           */
/* ========================================================================= */

/**
 * @brief Executes the complete SAT Logic JSON pipeline.
 *        Ingests the input JSON (format, solver_config, metadata, clauses, assumptions),
 *        executes Sodium's CDCL core, and populates the response object:
 *        - status ("SATISFIABLE" / "UNSATISFIABLE" / "UNKNOWN")
 *        - status_code (10 / 20 / 0)
 *        - statistics (variables, clauses)
 *        - raw_model ([+1, -2, ...])
 *        - assignment ({"VariableName": true/false, ...})
 *        - failed_assumptions_core ([...])
 *
 * @param json_request       Input JSON string following Mode 1 contract.
 * @param out_json_response  Pointer to dynamically allocated output JSON string.
 *                           Caller must free with sodium_api_free_string().
 * @return status_code (10 = SAT, 20 = UNSAT, 0 = UNKNOWN/ERROR).
 */
int sodium_api_solve_json(const char* json_request, char** out_json_response);

/**
 * @brief Constructs the Mode 2 NLP prompt payload:
 *        {
 *          "userPrompt": "<user_prompt>",
 *          "SystemPrompt": "<target JSON schema instructions for LLM conversion>"
 *        }
 *
 * @param user_prompt        The user-entered natural language prompt or specification.
 * @param out_payload_json   Pointer to dynamically allocated output JSON string.
 *                           Caller must free with sodium_api_free_string().
 * @return 0 on success, negative error code on failure.
 */
int sodium_api_build_nlp_payload(const char* user_prompt, char** out_payload_json);

/**
 * @brief Releases any string allocated and returned by the Sodium API.
 *
 * @param str                Pointer to string to free (safe with NULL).
 */
void sodium_api_free_string(char* str);

/* ========================================================================= */
/* 2. Direct In-Memory C Solver API (Referencing CaDiCaL Principles)         */
/* ========================================================================= */

typedef struct SodiumSolver SodiumSolver;

/**
 * @brief Allocates and initializes an in-memory Sodium solver instance.
 */
SodiumSolver* sodium_solver_create(void);

/**
 * @brief Cleans up and frees an in-memory Sodium solver instance.
 */
void          sodium_solver_destroy(SodiumSolver* solver);

/**
 * @brief Adds a literal to the current clause. A literal of 0 terminates the clause.
 *
 * @param solver Solver instance.
 * @param lit    Signed non-zero literal, or 0 to finish the clause.
 */
void          sodium_solver_add_literal(SodiumSolver* solver, int lit);

/**
 * @brief Adds a temporary assumption literal for the subsequent solve call only.
 *
 * @param solver Solver instance.
 * @param lit    Signed non-zero assumption literal.
 */
void          sodium_solver_add_assumption(SodiumSolver* solver, int lit);

/**
 * @brief Clears all current assumptions without modifying the base clause database.
 */
void          sodium_solver_clear_assumptions(SodiumSolver* solver);

/**
 * @brief Executes the CDCL search loop under current base clauses and assumptions.
 *
 * @param solver Solver instance.
 * @return 10 (SODIUM_STATUS_SATISFIABLE), 20 (SODIUM_STATUS_UNSATISFIABLE), or 0.
 */
int           sodium_solver_solve(SodiumSolver* solver);

/**
 * @brief Queries the truth assignment of a literal in the computed model.
 *
 * @param solver Solver instance.
 * @param lit    Signed variable literal.
 * @return Positive lit if true, -lit if false, 0 if unassigned or not solved.
 */
int           sodium_solver_get_value(SodiumSolver* solver, int lit);

/**
 * @brief Checks if an assumed literal was part of the failed assumptions core on UNSAT.
 *
 * @param solver Solver instance.
 * @param lit    Signed assumption literal.
 * @return true if lit is in the core, false otherwise.
 */
bool          sodium_solver_is_failed_assumption(SodiumSolver* solver, int lit);

/**
 * @brief Returns total distinct variables registered.
 */
int           sodium_solver_get_variable_count(const SodiumSolver* solver);

/**
 * @brief Returns total active base clauses registered.
 */
int           sodium_solver_get_clause_count(const SodiumSolver* solver);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_API_H
