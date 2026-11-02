#ifndef SODIUM_THEORY_EUF_H
#define SODIUM_THEORY_EUF_H

#include "../smt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns the theory interface vtable for Equality and Uninterpreted
 *        Functions (EUF) based on Congruence Closure.
 */
const SodiumTheoryInterface* sodium_theory_euf_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_THEORY_EUF_H
