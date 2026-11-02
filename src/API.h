#ifndef SODIUM_API_H
#define SODIUM_API_H

/**
 * @file API.h
 * @brief Unified Sodium Top-Level Facade Header.
 *        The codebase is organized into 4 distinct feature modules to reduce interdependencies:
 *        - SAT:             Conjunctive Normal Form & CDCL solving (src/SAT/sat_api.h)
 *        - NLP:             Semantic rule translation & prompt engineering (src/NLP/nlp_api.h)
 *        - InterfaceAndUI:  Dear ImGui frontend & guided rule builder (src/InterfaceAndUI/userInterface.h)
 *        - SMT:             Satisfiability Modulo Theories DPLL(T) engine (src/SMT/smt_core.h)
 */

#include "SAT/sat_api.h"
#include "NLP/nlp_api.h"
#include "SMT/smt_core.h"
#include "InterfaceAndUI/userInterface.h"

#endif // SODIUM_API_H
