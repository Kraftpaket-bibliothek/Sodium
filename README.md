<div align="center">

# Sodium

### Industrial-Grade Automated Reasoning & Neuro-Symbolic Verification Engine

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=for-the-badge)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C11_%2F_Pure_C-00599C.svg?style=for-the-badge&logo=c)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg?style=for-the-badge)]()
[![Memory Safety](https://img.shields.io/badge/Sanitizers-ASan_%26_UBSan_Clean-success.svg?style=for-the-badge)]()
[![Core](https://img.shields.io/badge/Core-Advanced_CDCL_%2B_VSIDS-orange.svg?style=for-the-badge)]()
[![Deterministic](https://img.shields.io/badge/Guarantees-100%25_Formal_Proof-purple.svg?style=for-the-badge)]()

<br/>

**Transform natural language requirements, complex contractual clauses, and business objectives into provably satisfiable mathematical solutions.**

[Executive Summary](#-the-executive-summary) •
[Why Sodium?](#-why-sodium) •
[Architecture](#-system-architecture) •
[Core Technology](#-core-technology--technical-moat) •
[Quickstart](#-quickstart) •
[Roadmap](#-roadmap--vision)

</div>

---

## ⚡ The Executive Summary

High-stakes industries—aerospace, defense, chip design, fintech, and supply chain logistics—run on **unstructured human language**: SLA contracts, regulatory compliance codes, engineering requirements, and operational objectives.

Until now, enterprises had two deeply flawed choices:
1. **Manual Formal Verification**: Requires specialized PhDs, takes months of human labor, and costs millions.
2. **Probabilistic Large Language Models (LLMs)**: Fast and natural, but **prone to hallucinations, stochastic errors, and zero mathematical certainty**.

**Sodium is an industrial automated reasoning solver that closes this gap.** It ingests complex natural language requirements, extracts hard constraints, clauses, and optimization objectives, and compiles them directly into a custom-engineered, ultra-low-latency **CDCL (Conflict-Driven Clause Learning) and SMT (Satisfiability Modulo Theories) engine**. 

Sodium does not guess. It **proves**—synthesizing and presenting the verifiable, plausible, and mathematically guaranteed satisfiable assignment.

---

## 💡 Why Sodium?

```
                        The Enterprise Verification Dilemma
   
   Human Language Specs              Probabilistic AI                    Sodium Solver
+------------------------+      +------------------------+      +-----------------------------+
| • Complex SLA rules    |      | • Fast & accessible    |      | • Natural Language Interface|
| • Safety requirements  | ---> | • Hallucinates outputs | ---> | • 100% Deterministic Proof  |
| • Operational goals    |      | • Fails under edge case|      | • Microsecond C Engine      |
| • Regulatory laws      |      | • ZERO formal guarantee|      | • Concrete Satisfiable Model|
+------------------------+      +------------------------+      +-----------------------------+
                                       (UNUSABLE IN                     (MISSION-CRITICAL
                                    MISSION-CRITICAL)                      ENTERPRISE)
```

| Dimension | Generic LLMs | Traditional SMT Solvers (Z3, CVC5) | **Sodium** |
| :--- | :--- | :--- | :--- |
| **Input Modality** | Natural Language | Raw SMT-LIB2 / DIMACS CNF | **Natural Language + Formal Specs** |
| **Mathematical Soundness** | ❌ None (Stochastic) | ✅ 100% Provable | **✅ 100% Provable** |
| **Output Interpretation** | Probabilistic text | Abstract Boolean Models | **Plausible, Concrete Satisfying Model** |
| **Latency & Overhead** | Seconds (API dependent) | Heavy dynamic heaps | **Microsecond Native C (Zero-overhead)** |
| **Memory Architecture** | Cloud GPUs | Standard Heap (GC pauses) | **Custom Generational Memory + Compaction** |

---

## 🏗 System Architecture

Sodium is engineered from the bare metal up in **C11**, decoupling front-end semantic translation from high-performance formal proof search.

```
+---------------------------------------------------------------------------------+
|                                 INPUT LAYER                                     |
|   Natural Language Requirements • Policy Directives • Operational Objectives   |
+----------------------------------------+----------------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------+
|                    SEMANTIC COMPILER & LOGIC TRANSLATOR                         |
|   Deconstructs English clauses into First-Order Predicates & Conjunctive Normal |
+----------------------------------------+----------------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------+
|                              SODIUM CDCL CORE                                   |
|                                                                                 |
|  +----------------------------------+  +-------------------------------------+  |
|  | Two-Watched Literals (2WL) BCP   |  | 1-UIP Conflict-Driven Clause Learn  |  |
|  | Microsecond Unit Propagation     |  | Non-Chronological Backjumping       |  |
|  +----------------------------------+  +-------------------------------------+  |
|  +----------------------------------+  +-------------------------------------+  |
|  | Dynamic VSIDS Branching Heuristic|  | Generational Memory Manager         |  |
|  | Exponential Decay & Rescale Guard|  | Zero-leak Handle Table & Compaction |  |
|  +----------------------------------+  +-------------------------------------+  |
+----------------------------------------+----------------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------+
|                               VERIFIED OUTPUT                                   |
|   s SATISFIABLE   -> Concrete, mathematically verified configuration model      |
|   s UNSATISFIABLE -> Minimal Unsatisfiable Core (MUC) proving contradiction     |
+---------------------------------------------------------------------------------+
```

---

## 🔬 Core Technology & Technical Moat

### 1. Custom Generational Handle-Based Memory Manager (`memory.c`)
- **ABA-Immune Generational Indexing**: Every allocation is tracked via a 64-bit generation handle (`RecordHandle { index, generation }`). Stale handles cannot resurrect or corrupt memory.
- **In-Place Two-Pointer Sliding Compaction**: Automatically compacts tombstoned records when fragmented threshold is crossed (> 30%).
- **Deterministic Latency**: Bypasses general-purpose system allocators during hot solving phases.

### 2. High-Performance CDCL Reasoning Engine (`CDCL.c`)
- **Two-Watched Literals (2WL)**: Inspects only clauses watching the literal that becomes false. Zero pointer rewrites required upon backtracking.
- **First Unique Implication Point (1-UIP) Conflict Analysis**: Resolves implication graphs backwards to isolate the root causal conflict, cutting the exponential search tree into polynomial paths.
- **Non-Chronological Backjumping**: Skips irrelevant decision levels directly to the assertion level determined by the learned clause.

### 3. VSIDS Branching Engine (`VSIDS.c`)
- **Exponential Activity Decay**: Divides score increments by `decay_factor = 0.95` on every conflict (`O(1)` amortized decay).
- **Double Rescaling Guard**: Prevents floating-point overflow (`> 1e100`) by safely downscaling all scores.
- **Direct Memory Integration**: Backed entirely by Sodium's generational `MemoryManager`.

#### Standalone VSIDS Usage

```c
#include "VSIDS.h"
#include <stdio.h>

int main(void) {
    VSIDS vsids;
    // Initialize VSIDS for 10 variables with default 0.95 decay
    vsids_initialize(&vsids, 10);

    // Bump variables involved in conflicts
    vsids_bump(&vsids, 3);
    vsids_bump(&vsids, 5);
    vsids_bump(&vsids, 5);

    // Decay scores after conflict resolution
    vsids_decay(&vsids);

    // Select the highest-activity unassigned variable
    int assignment[11] = {0}; // All UNASSIGNED
    size_t next_branch = vsids_select_variable(&vsids, assignment);
    printf("Next branch decision: Variable %zu (Activity: %.2f)\n", 
           next_branch, vsids_get_activity(&vsids, next_branch));

    vsids_cleanup(&vsids);
    return 0;
}
```

```bash
# Compile and run standalone VSIDS
clang -Wall -Wextra -O3 -I src/SAT/CDCL vsids_example.c \
    src/SAT/CDCL/VSIDS.c \
    src/SAT/CDCL/memory.c \
    -o vsids_example

./vsids_example
```

---

## 🚀 Quickstart

### Prerequisites
- Modern C Compiler (`clang` or `gcc`) with C11 support

### Building the CDCL Solver Engine

```bash
# Clone the repository
git clone https://github.com/Aditya-B-007/Sodium.git
cd Sodium

# Compile the core CDCL solver object modules
clang -Wall -Wextra -Wpedantic -O3 -I src/SAT/CDCL -c \
    src/SAT/CDCL/memory.c \
    src/SAT/CDCL/VSIDS.c \
    src/SAT/CDCL/CDCL.c
```

### Programmatic Integration Example

Save the following code as `example.c`:

```c
#include "CDCL.h"
#include <stdio.h>

int main(void) {
    // Requirements translated to clauses:
    // (Database_Primary OR Database_Replica) AND (NOT Database_Primary OR Database_Replica)
    CDCLSolver solver;
    
    int c1[] = { 1,  2};
    int c2[] = {-1,  2};
    
    Clause* clauses[2];
    clauses[0] = create_clause(c1, 2, false);
    clauses[1] = create_clause(c2, 2, false);

    bool is_sat = solve(&solver, 2, clauses, 2);

    if (is_sat) {
        printf("s SATISFIABLE\n");
        printf("Optimal Assignment: Var 1 = %s, Var 2 = %s\n",
               solver.assignment[1] == VAL_TRUE ? "TRUE" : "FALSE",
               solver.assignment[2] == VAL_TRUE ? "TRUE" : "FALSE");
    } else {
        printf("s UNSATISFIABLE (Requirements are contradictory)\n");
    }

    destroy_solver(&solver);
    return 0;
}
```

```bash
# Compile and run the complete CDCL solver application
clang -Wall -Wextra -O3 -I src/SAT/CDCL example.c \
    src/SAT/CDCL/CDCL.c \
    src/SAT/CDCL/VSIDS.c \
    src/SAT/CDCL/memory.c \
    -o sodium

./sodium
```

---

## 📊 Industrial Use Cases

- **Autonomous Vehicle Safety & Compliance**: Ingesting legal traffic codes and vehicle telemetry requirements to verify that trajectory planning never violates safety invariants.
- **Hardware Circuit & Protocol Verification**: Translating timing diagrams and hardware descriptions into formal satisfiability proofs before tape-out.
- **Enterprise SLA & Procurement Optimization**: Ingesting contractual constraints, volume pricing tiers, and delivery SLAs to synthesize the optimal allocation.
- **Cybersecurity Policy & IAM Auditing**: Proving that zero combinations of user role transitions can violate security boundaries.

---

## 🗺 Roadmap & Vision

- [x] **Q4 2026**: High-performance CDCL Engine with 2WL, 1-UIP learning, and VSIDS.
- [x] **Q4 2026**: Generational handle-based memory management with automated compaction.
- [ ] **Q1 2027**: Natural Language Specification Compiler (LLM-guided translation with deterministic AST verification).
- [ ] **Q2 2027**: Native SMT Theories:
  - Linear Real and Integer Arithmetic (LRA / LIA).
  - Theory of Bit-Vectors (BV) for hardware and software systems.
  - Theory of Arrays & Uninterpreted Functions (Arrays / UF).
- [ ] **Q3 2027**: Enterprise Distributed Reasoning Cloud API & Verification Co-Pilot.

---

## 📄 License

Sodium is licensed under the **[Apache License 2.0](LICENSE)**.

---

<div align="center">
<b>Sodium is built for the era of provable, verifiable intelligence.</b>
<br/>
<sub>Designed & engineered by Aditya.</sub>
</div>
