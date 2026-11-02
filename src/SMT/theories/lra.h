#ifndef SODIUM_THEORY_LRA_H
#define SODIUM_THEORY_LRA_H

#include "../smt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns the theory interface vtable for Linear Real Arithmetic (LRA)
 *        utilizing the DPLL(T)-specialized Simplex algorithm.
 */
const SodiumTheoryInterface* sodium_theory_lra_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_THEORY_LRA_H
