# Sodium SMT: Satisfiability Modulo Theories Engine

This directory contains Sodium's **DPLL(T)** SMT engine, designed to lift propositional Boolean reasoning (powered by Sodium's CDCL core) to rich first-order background theories.

---

## 🏛 Architecture: The DPLL(T) Framework

Sodium implements the classical **DPLL(T)** architecture, cleanly separating propositional satisfiability search from first-order theory consistency checks:

```
                      +-----------------------------+
                      |   Propositional Skeleton    |
                      |   (Sodium CDCL SAT Core)    |
                      +--------------+--------------+
                                     |
               Boolean Assignments   |   T-Lemmas (Conflict Clauses)
               & Deductions          |   & T-Propagations
                                     v
                      +-----------------------------+
                      |     Theory Coordinator      |
                      |       (`smt_core.c`)        |
                      +--------------+--------------+
                                     |
             +-----------------------+-----------------------+
             |                       |                       |
             v                       v                       v
      +--------------+        +--------------+        +--------------+
      |  Theory: EUF |        | Theory: LRA  |        | Theory: BV   |
      | (Congruence  |        | (Dual Simplex|        |  (Bit-Blast  |
      |   Closure)   |        |  Tableaux)   |        |  DPLL(T))    |
      +--------------+        +--------------+        +--------------+
```

### Key Components

1. **`smt_core.h` / `smt_core.c`**:
   - Manages propositional-to-theory abstraction mappings.
   - Dispatches assignments to registered theory solvers.
   - Learns minimal conflict explanations ($T$-lemmas) and feeds them back into the CDCL database to prune invalid search spaces.

2. **Decoupled Theory Interfaces (`SodiumTheoryInterface`)**:
   - Each theory solver implements a standardized function pointer vtable:
     - `assert_literal`: Ingests an assigned theory atom.
     - `check_consistency`: Determines whether current theory literals are mutually satisfiable.
     - `explain_conflict`: Synthesizes an unsatisfiable core of theory literals to form a learned conflict clause.
     - `push_level` / `pop_level`: Backtracks solver state alongside CDCL decision levels.
     - `reset`: Clears current state for subsequent queries.

3. **Planned & Pluggable Theories (`theories/`)**:
   - **EUF (`theories/euf.h`)**: Equality with Uninterpreted Functions via Congruence Closure.
   - **LRA (`theories/lra.h`)**: Linear Real Arithmetic via incrementing Simplex tableaux.
   - **LIA**: Linear Integer Arithmetic via Branch-and-Cut.
   - **BV**: Fixed-width bit-vector arithmetic.
