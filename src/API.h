#ifndef SODIUM_API_H
#define SODIUM_API_H

/**
 * @file API.h
 * @brief Unified Sodium Top-Level Facade Header.
 *        The codebase is organized into distinct feature modules:
 *        - SAT:             Conjunctive Normal Form & CDCL solving (src/SAT/sat_api.h)
 *        - NLP:             Semantic rule translation & prompt engineering (src/NLP/nlp_api.h)
 *        - InterfaceAndUI:  Dear ImGui frontend & guided rule builder (src/InterfaceAndUI/userInterface.h)
 *        - SMT:             Satisfiability Modulo Theories (src/SMT/)
 */

#include "SAT/sat_api.h"
#include "NLP/nlp_api.h"
#include "InterfaceAndUI/userInterface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parses a DIMACS CNF formula string, executes CDCL solving, and generates
 *        a Mode 1 JSON response string.
 *
 * @param dimacs_str         Input DIMACS CNF string (e.g., "p cnf 3 2\n1 2 0\n-1 3 0").
 * @param out_json_response  Pointer to dynamically allocated JSON string. Free with sodium_api_free_string().
 * @return status_code (10 = SAT, 20 = UNSAT, 0 = UNKNOWN).
 */
int sodium_api_solve_dimacs(const char* dimacs_str, char** out_json_response);

/**
 * @brief C-level verification bridge designed for the C++ Dear ImGui frontend
 *        (src/InterfaceAndUI/userInterface.cpp). Solves the formula and returns status,
 *        truth model array, and elapsed time in milliseconds.
 *
 * @param formula_text       DIMACS or logic formula text.
 * @param out_status         Returned status (10 = SAT, 20 = UNSAT, 0 = UNKNOWN).
 * @param out_model          Dynamically allocated array of literals (index 1..max_var). Caller frees with free().
 * @param out_max_var        Highest variable index discovered.
 * @param out_time_ms        Elapsed solve time in milliseconds.
 * @return 0 on success, negative error code on failure.
 */
int sodium_api_verify_formula(
    const char* formula_text,
    int* out_status,
    int** out_model,
    int* out_max_var,
    double* out_time_ms
);

/**
 * @brief Signals interruption to any running CDCL solver operation.
 */
void sodium_api_interrupt(void);

/**
 * @brief Resets solver telemetry and global statistics.
 */
void sodium_api_reset(void);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_API_H
