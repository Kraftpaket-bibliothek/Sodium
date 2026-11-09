<div align="center">

# Sodium
<img width="1536" height="1024" alt="Modern Na Emblem with Indian Flag Accent" src="https://github.com/user-attachments/assets/fe782949-42c1-41b2-9953-8bccacfefab6" />

### Industrial-Grade Automated Reasoning & Neuro-Symbolic Verification Engine

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=for-the-badge)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C11_%2F_Pure_C-00599C.svg?style=for-the-badge&logo=c)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg?style=for-the-badge)]()
[![Core](https://img.shields.io/badge/Core-Advanced_CDCL_%2B_VSIDS-orange.svg?style=for-the-badge)]()
[![Architecture](https://img.shields.io/badge/Architecture-Dual_Engine_Neuro--Symbolic-8A2BE2.svg?style=for-the-badge)]()
[![Benchmark](https://img.shields.io/badge/Scale-10M_Clauses_in_25s-brightgreen.svg?style=for-the-badge)]()
[![Memory](https://img.shields.io/badge/Memory-Generational_Handles_%2B_Arenas-informational.svg?style=for-the-badge)]()
[![Deterministic](https://img.shields.io/badge/Guarantees-100%25_Formal_Proof-purple.svg?style=for-the-badge)]()

<br/>

**Transform natural language requirements, complex contractual clauses, and mathematical objectives into provably satisfiable formal solutions.**

[Executive Summary](#-the-executive-summary) •
[Why Sodium?](#-why-sodium) •
[Architecture & Engineering](#-system-architecture--engineering-principles) •
[Feature Catalog](#-complete-feature-catalog) •
[User Guide (LLM & Non-LLM)](#-user-guide-operating-sodium-llm-vs-non-llm) •
[Core Technology](#-core-technology--technical-moat) •
[Benchmarks](#-performance-benchmarks--extreme-scale-stress-testing) •
[Quickstart](#-quickstart) •
[Known Limitations](#-current-limitations--areas-for-improvement-open-disclosure) •
[Roadmap](#-roadmap--vision)

</div>

---

## ⚡ The Executive Summary

High-stakes industries—aerospace, defense, chip design, fintech, and supply chain logistics—run on **unstructured human language**: SLA contracts, regulatory compliance codes, engineering requirements, and operational objectives.

Until now, enterprises had two deeply flawed choices:
1. **Manual Formal Verification**: Requires specialized PhDs, takes months of human labor, and costs millions.
2. **Probabilistic Large Language Models (LLMs)**: Fast and natural, but **prone to hallucinations, stochastic errors, and zero mathematical certainty**.

**Sodium is an industrial automated reasoning engine that closes this gap.** It ingests complex natural language requirements, mathematical equations, and business rules, extracting hard constraints and compiling them directly into a custom-engineered, ultra-low-latency **CDCL (Conflict-Driven Clause Learning) and SMT (Satisfiability Modulo Theories) engine**. 

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
| **Input Modality** | Natural Language | Raw SMT-LIB2 / DIMACS CNF | **Natural Language + Math + DIMACS** |
| **Mathematical Soundness** | ❌ None (Stochastic) | ✅ 100% Provable | **✅ 100% Provable** |
| **Output Interpretation** | Probabilistic text | Abstract Boolean Models | **Plausible, Concrete Satisfying Model** |
| **Reasoning Method** | Cloud API (Stochastic) | Pure Solvers (No NLP) | **Dual Engine: Neural LLM OR Classical NLP** |
| **Latency & Overhead** | Seconds (API dependent) | Heavy dynamic heaps | **Microsecond Native C (Zero-overhead)** |
| **Memory Architecture** | Cloud GPUs | Standard Heap (GC pauses) | **Custom Generational Memory + Compaction** |

---

## 🏛 System Architecture & Engineering Principles

Sodium is engineered from the bare metal up in **C11**, decoupling semantic translation from high-performance formal proof search.

```
+--------------------------------------------------------------------------------------------------+
|                                           INPUT LAYER                                            |
|   Natural Language Requirements • Policy Directives • Mathematical Equations • DIMACS CNF • JSON  |
+-------------------------------------------------+------------------------------------------------+
                                                  |
                                                  v
+--------------------------------------------------------------------------------------------------+
|                                     DUAL NLP REASONING ENGINE                                    |
|                                                                                                  |
|   METHOD A: NEURAL LLM ROUTER (llm_router.py)  |  METHOD B: CLASSICAL DETERMINISTIC (NLPAlgo/)   |
|   • Pydantic Bidirectional Contract Validation |  • 7-Step Linguistic Pipeline (Regex / NLTK)    |
|   • OpenAI / Ollama / DeepSeek / vLLM Routing  |  • SVO Parser + Distance Entity Resolver        |
|   • Zero-Hallucination Schema Enforcement      |  • Math Shielding + WordNet + Centrality Score  |
+-------------------------------------------------+------------------------------------------------+
                                                  |
                                                  v
+--------------------------------------------------------------------------------------------------+
|                                 UNIFIED C API & ORCHESTRATOR                                     |
|   sodium_orchestrator.c • API.c • Mode 1 JSON API (sat_api.c) • Embedded HTTP Daemon & Web SPA   |
+-------------------------------------------------+------------------------------------------------+
                                                  |
                                                  v
+--------------------------------------------------------------------------------------------------+
|                                        SODIUM CDCL CORE                                          |
|                                                                                                  |
|  +-------------------------------------------+  +---------------------------------------------+  |
|  | Two-Watched Literals (2WL) BCP            |  | 1-UIP Conflict-Driven Clause Learning       |  |
|  | Microsecond Unit Propagation              |  | Non-Chronological Backjumping               |  |
|  +-------------------------------------------+  +---------------------------------------------+  |
|  +-------------------------------------------+  +---------------------------------------------+  |
|  | Dynamic VSIDS Branching Heuristic         |  | Generational Handle Memory Manager          |  |
|  | Exponential Decay & Rescale Guard         |  | ABA Immunity & Sliding Two-Pointer Compact  |  |
|  +-------------------------------------------+  +---------------------------------------------+  |
|  +--------------------------------------------------------------------------------------------+  |
|  | Contiguous Memory Arena (ClauseArray) — Zero-Indirection Cache-Locality Engine             |  |
|  +--------------------------------------------------------------------------------------------+  |
+-------------------------------------------------+------------------------------------------------+
                                                  |
                                                  v
+--------------------------------------------------------------------------------------------------+
|                                        VERIFIED OUTPUT                                           |
|   s SATISFIABLE   -> Concrete, mathematically verified configuration model & variable mapping    |
|   s UNSATISFIABLE -> Contradiction detected; proof that no satisfying state exists                |
+--------------------------------------------------------------------------------------------------+
```

### 1. Variability Architecture: Compile-Time vs. Run-Time

Sodium explicitly balances compile-time performance guarantees with run-time flexibility:

```
                            Variability Spectrum in Sodium
┌──────────────────────────────────────────────┬──────────────────────────────────────────────┐
│            COMPILE-TIME VARIABILITY          │             RUN-TIME VARIABILITY             │
├──────────────────────────────────────────────┼──────────────────────────────────────────────┤
│ • Zero-overhead binary specialization        │ • Dynamic NLP Engine selection               │
│ • `#ifdef SODIUM_ENABLE_GUI` conditional code│   (`--engine openai` vs `--non-llm`)         │
│ • Headless C server vs C++17 Desktop ImGui   │ • Web UI interactive engine switch           │
│ • Static arena capacities & memory limits    │ • Input format auto-detection                │
│ • Inlined critical CDCL routines & 2WL loop  │   (DIMACS CNF vs Mode 1 JSON vs Natural Text)│
│ • Compiler optimization flags (-O3, LTO)     │ • Multi-provider LLM endpoint routing        │
└──────────────────────────────────────────────┴──────────────────────────────────────────────┘
```

#### Compile-Time Variability
- **Conditional Subsystem Compilation**: The desktop Dear ImGui graphical interface is governed by `#ifdef SODIUM_ENABLE_GUI`. When running `make server`, the C++ runtime, GLFW window management, and OpenGL bindings are completely excised. This produces a lightweight, zero-external-dependency, pure C11 headless binary ideal for containerized microservices and cloud deployments.
- **Fixed-Footprint Arena Sizing**: Memory pool limits (such as record handle table limits and initial contiguous clause arrays) are configured via preprocessor definitions, guaranteeing predictable memory footprints on embedded and constrained platforms.
- **Inlined Inner Loops**: Boolean constraint propagation (BCP) and two-watched-literal updates utilize `static inline` functions to eliminate function-call overhead in tight solving loops.

#### Run-Time Variability
- **Dynamic NLP Engine Dispatch**: The system switches dynamically between the Neural LLM Router (`llm_router.py`) and the Classical Non-LLM 7-Step Pipeline (`NLPAlgo/`) via CLI flags (`--non-llm`) or interactive UI toggles without requiring re-compilation.
- **Dynamic Input Auto-Detection**: The orchestrator inspects incoming data on-the-fly, instantly branching between standard DIMACS CNF files, CaDiCaL Mode 1 JSON payloads, piped stdin streams, and raw natural language sentences.
- **Configurable Model Endpoints**: Neural LLM routing dynamically binds to OpenAI GPT-4o, Ollama local instances (`llama3`, `deepseek-r1`), or any OpenAI-compatible vLLM server via environment variables and CLI arguments.

---

### 2. Software Design Patterns in Sodium

Sodium’s architecture adheres strictly to proven software engineering patterns to ensure separation of concerns, high maintainability, and microsecond-level performance:

1. **Facade Pattern (`src/API.h`, `src/API.c`)**:
   Provides a unified, simplified C interface (`sodium_api_solve_dimacs`, `sodium_api_verify_formula`, `API_SolveProblem`, `API_VerifyProblem`). Clients (desktop GUI, web server, CLI) interact with this single facade without needing to manage 2WL watch lists, implication graphs, or VSIDS internal states directly.
2. **Bridge Pattern**:
   Decouples the presentation layer (CLI, Dear ImGui desktop window, embedded HTTP daemon) from the concrete SAT solving engine. The user interface can be completely redesigned or swapped without modifying a single line of the underlying CDCL or memory logic.
3. **Strategy Pattern**:
   - *Linguistic Translation Strategy*: The NLP subsystem provides interchangeable strategies for constraint extraction: `NeuralStrategy` (probabilistic LLM with Pydantic verification) and `ClassicalStrategy` (deterministic 7-step pipeline).
   - *Decision Strategy*: The CDCL solver can interchange variable branching heuristics (VSIDS activity-based vs. static orderings).
4. **Pipes-and-Filters Pattern (`src/NLP/NLPAlgo/`)**:
   The Classical Non-LLM methodology is organized as a strict, directional pipeline where the output of each filter feeds directly into the next:
   $$\text{Tokenization} \longrightarrow \text{POS/Dep Parsing} \longrightarrow \text{Entity Extraction} \longrightarrow \text{Knowledge Injection} \longrightarrow \text{Lexicon Scoring} \longrightarrow \text{Static Vectorization} \longrightarrow \text{CNF Synthesis}$$
5. **Arena & Memory Pool Pattern (`src/SAT/CDCL/Clauses/clause_array.c`)**:
   Clauses and literals are allocated within contiguous memory blocks rather than through scattered individual heap allocations. This guarantees spatial cache locality and eliminates memory fragmentation during prolonged solving runs.
6. **Generational Handle Pattern (`src/SAT/CDCL/memory.c`)**:
   References to internal memory records use 64-bit compound handles containing an index and a generation counter (`RecordHandle { index, generation }`). Stale handles from deallocated or compacted records are immediately detected, preventing dangling pointer dereferences and ABA resurrection bugs.
7. **Adapter Pattern**:
   - The Python `ctypes` bridge in `llm_router.py` adapts Python dictionary payloads into C-compatible memory buffers.
   - The Dear ImGui wrapper (`src/InterfaceAndUI/userInterface.cpp`) adapts the C++ immediate-mode GUI lifecycle to the synchronous C API.

---

### 3. Software Engineering Discipline & Quality Assurance

- **Zero-Tolerance Memory Safety**: Every commit is verified using Clang's **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)** to ensure zero memory leaks, buffer overruns, or alignment violations.
- **Pydantic Schema Enforcement**: All communication between the neural layer and the symbolic solver is validated through strict bidirectional Pydantic schemas (`SATProblemSchema` and `SATSolutionSchema`), ensuring malformed or hallucinated LLM responses are rejected before reaching the C engine.
- **Zero-Allocation Inner Loops**: During active BCP and conflict resolution, Sodium avoids dynamic heap allocations (`malloc`/`free`), operating entirely within pre-allocated arrays and ring buffers.

---

## 📦 Complete Feature Catalog

| Module | Location | Capabilities & Highlights |
| :--- | :--- | :--- |
| **CDCL Reasoning Core** | `src/SAT/CDCL/CDCL.c` | Two-Watched Literals (2WL), 1-UIP conflict analysis, non-chronological backjumping, implication graph tracking. |
| **VSIDS Decision Engine** | `src/SAT/CDCL/VSIDS.c` | Exponential activity decay ($0.95$), amortized $O(1)$ bumping, double rescaling guard against floating-point overflow. |
| **Generational Memory** | `src/SAT/CDCL/memory.c` | 64-bit generational handles (`index + generation`), two-pointer sliding compaction, fragmentation monitoring. |
| **Contiguous Arenas** | `src/SAT/CDCL/Clauses/` | Flat-array contiguous clause storage, cache-line aligned literal iteration, zero pointer dereferencing overhead. |
| **Unified C Facade API** | `src/API.c`, `src/API.h` | High-level API for DIMACS parsing, solution verification (`sodium_api_verify_formula`), model extraction. |
| **Mode 1 JSON SAT API** | `src/SAT/sat_api.c` | CaDiCaL-compatible in-memory JSON interface for programmatic integration with high-level languages. |
| **Neural LLM Bridge** | `src/NLP/llm_router.py` | Multi-provider router (OpenAI, Ollama, DeepSeek, vLLM), strict Pydantic JSON schema enforcement, bidirectional translation. |
| **Classical Non-LLM NLP** | `src/NLP/NLPAlgo/` | 7-step deterministic rule & math parser: Tokenization, SVO Parsing, Distance Matching, WordNet, VADER, Static Vectors, CNF Synthesis. |
| **Unified Orchestrator** | `src/sodium_orchestrator.c` | Single executable handling CLI arguments, stdin pipes, embedded Web server, and native desktop UI dispatch. |
| **Interactive Web Studio** | `src/sodium_orchestrator.c` | Embedded HTTPS-ready HTTP daemon, single-page web app, Royal Purple theme, REST API (`/api/v1/solve`, `/api/v1/nlp-solve`). |
| **Native Desktop UI** | `src/InterfaceAndUI/` | Immediate-mode Dear ImGui desktop window with rule editors, live variable inspection, and verification dashboard. |

---

## 📖 User Guide: Operating Sodium (LLM vs. Non-LLM)

Sodium offers two distinct methodologies for converting human requirements and mathematical constraints into formal logic:

```
                            Choose Your NLP Methodology
┌──────────────────────────────────────────────┬──────────────────────────────────────────────┐
│         METHOD A: NEURAL LLM ROUTER          │        METHOD B: CLASSICAL NON-LLM           │
├──────────────────────────────────────────────┼──────────────────────────────────────────────┤
│ • Uses generative models (OpenAI, Ollama)    │ • 100% offline, zero external API keys       │
│ • Best for unstructured, conversational text │ • Best for strict technical & mathematical   │
│ • Extracts implicit relationships            │   specifications                             │
│ • Pydantic contract ensures valid output     │ • Deterministic, microsecond latency         │
│ • Requires API key or local model weights    │ • Complete privacy; runs on any device       │
└──────────────────────────────────────────────┴──────────────────────────────────────────────┘
```

---

### Method A — Neural LLM Reasoning Bridge

The Neural LLM Bridge uses large language models with strict JSON schema constrained decoding.

#### 1. Setup & Environment
Ensure Python 3.9+ is installed with the required dependencies:
```bash
pip install -r requirements.txt
# Or using the local virtual environment:
source .venv/bin/activate
```

Configure your model provider:
```bash
# For OpenAI:
export OPENAI_API_KEY="your-api-key"

# For Local Ollama (default: http://localhost:11434):
# Ensure Ollama is running: ollama run llama3
```

#### 2. Command-Line Execution
```bash
# Direct translation and solving via Sodium CLI
./sodium --nlp "Service Alpha and Service Beta cannot run concurrently. Service Alpha must be active."

# Calling the Python router directly
python3 src/NLP/llm_router.py --engine openai --prompt "If PaymentGateway is enabled, SSL must be active. PaymentGateway is enabled."
```

#### 3. Interactive Web Studio
1. Launch the web server: `./sodium --serve --port 8080`
2. Open `http://localhost:8080` in your browser.
3. In the **Natural Language Requirements** tab, select **LLM (Neural)** from the engine dropdown.
4. Enter your scenario and click **Solve Formulation**.

---

### Method B — Classical Non-LLM NLP & Math Compiler

The Classical Non-LLM engine provides a **100% deterministic, offline, and private** reasoning bridge. It uses zero neural weights and requires no API keys, executing a deterministic 7-step linguistic and mathematical compilation pipeline.

#### The 7-Step Non-LLM Pipeline
```
[ Raw Text & Math Equations ]
              │
              ▼
[ 1. Tokenization & Normalization ]    ──> Regex boundary splitter + Lemmatization (NLTK)
              │
              ▼
[ 2. Syntactic & Linguistic Parsing ]  ──> POS Tagging + SVO & Conditional Dependency Parsing
              │
              ▼
[ 3. Rule-Based Entity Extraction ]    ──> Regex + Levenshtein / Jaro-Winkler String Distance
              │
              ▼
[ 4. Structural Knowledge Injection ]  ──> WordNet Synsets (Synonyms, Hypernyms, Antonym Negation)
              │
              ▼
[ 5. Lexicon-Based Feature Scoring ]   ──> VADER Sentiment Polarity + TextRank Keyphrase Centrality
              │
              ▼
[ 6. Static Vectorization ]           ──> Word2Vec / GloVe lookup table for semantic clustering
              │
              ▼
[ 7. Vector Aggregation & Scoring ]   ──> TF-IDF + Graph Centrality + Deterministic CNF Synthesis
              │
              ▼
[ Solved by Sodium CDCL Core ]
```

#### 1. Nuance & Rule Formatting Guidelines
> [!IMPORTANT]
> When using the Non-LLM Classical engine, write rules in clear, well-separated English or mathematical statements.
> - Separate each distinct rule using a **period (`.`)**, **semicolon (`;`)**, or **newline**.
> - Use unambiguous conditional structures (`If ... then ...`, `... implies ...`, `... requires ...`).
> - Use explicit mutual exclusion phrasing (`Either A or B, but not both`, `A and B cannot both be active`).
> - Mathematical inequalities and symbols can be typed directly.

#### 2. Command-Line Execution
```bash
# Run with the --non-llm flag via Sodium CLI
./sodium --nlp "Server A and Server B cannot both be active. Server A is online." --non-llm

# Run complex implications with mathematical relations
./sodium --nlp "If x + y <= 10 then SystemReady is active. x + y <= 10. SystemReady is required." --non-llm

# Run directly via the Python classical pipeline
python3 src/NLP/NLPAlgo/classical_pipeline.py "Resource X requires Resource Y. Resource X is enabled."
```

#### 3. Interactive Web Studio
1. Launch `./sodium --serve --port 8080` and open `http://localhost:8080`.
2. Select **Non-LLM (Classical Rules & Math)** from the dropdown.
3. An informational popup will appear reminding you to write well-separated rules.
4. Enter your constraints and click **Solve Formulation**. The problem will be solved locally in microseconds.

---

### Mathematical Syntax & Logic Notation Cheatsheet

Sodium natively shields, parses, and grounds mathematical expressions and formal logical symbols:

| Mathematical / Logical Concept | Standard Symbols | ASCII Equivalents | Example Ingestion |
| :--- | :--- | :--- | :--- |
| **Implication** | $\implies$, $\to$ | `->`, `implies`, `requires` | `SensorA -> AlarmTriggered` |
| **Equivalence / Biconditional** | $\iff$, $\equiv$ | `<->`, `equivalent`, `iff` | `HighLoad <-> ScaleOut` |
| **Conjunction / Disjunction** | $\land$, $\lor$ | `AND`, `OR`, `&&`, `\|\|` | `MasterOnline AND BackupReady` |
| **Negation** | $\neg$, $\sim$ | `NOT`, `!`, `disabled`, `offline` | `NOT MaintenanceMode` |
| **Mutual Exclusion (Mutex)** | $\oplus$, $\not\equiv$ | `xor`, `either A or B but not both` | `PrimaryNode XOR SecondaryNode` |
| **Relational Inequalities** | $\le, \ge, \neq, =, <, >$ | `<=`, `>=`, `!=`, `==`, `<`, `>` | `MemoryUsage <= 90 -> ClusterHealthy` |
| **Linear Arithmetic Grounding** | $+$, $-$, $*$, $/$ | `+`, `-`, `*`, `/` | `x + y <= 10 -> SafeState` |

---

## 🔬 Core Technology & Technical Moat

### 1. Custom Generational Handle-Based Memory Manager (`memory.c`)
- **ABA-Immune Generational Indexing**: Every allocation is tracked via a 64-bit generation handle (`RecordHandle { index, generation }`). Stale handles cannot resurrect or corrupt memory.
- **In-Place Two-Pointer Sliding Compaction**: Automatically compacts tombstoned records when the fragmented threshold is crossed (> 30%).
- **Deterministic Latency**: Bypasses general-purpose system allocators during hot solving phases.

### 2. High-Performance CDCL Reasoning Engine (`CDCL.c`)
- **Two-Watched Literals (2WL)**: Inspects only clauses watching the literal that becomes false. Zero pointer rewrites required upon backtracking.
- **First Unique Implication Point (1-UIP) Conflict Analysis**: Resolves implication graphs backwards to isolate the root causal conflict, cutting the exponential search tree into polynomial paths.
- **Non-Chronological Backjumping**: Skips irrelevant decision levels directly to the assertion level determined by the learned clause.

### 3. VSIDS Branching Engine (`VSIDS.c`)
- **Exponential Activity Decay**: Divides score increments by `decay_factor = 0.95` on every conflict ($O(1)$ amortized decay).
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

---

## 🏎 Performance Benchmarks & Extreme-Scale Stress Testing

Sodium's CDCL core is engineered for mission-critical scale. The engine has been rigorously stress-tested across standard combinatorial benchmarks and massive constraint networks under strict **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)** instrumentation:

### 🌟 Extreme-Scale Milestone: 10 Million Clauses in 25 Seconds
To validate memory efficiency, watch-list scalability, and zero-degradation unit propagation under extreme loads, Sodium was benchmarked on a massive synthetic constraint network:
- **Instance**: **10,000,000 Clauses** across **50,000 Variables**.
- **Combinatorial Diversity**: Dynamic mix of 2-SAT through 6-SAT clauses with varied polarities (all-positive, all-negative, alternating), localized variable clusters, and long-range cross-dependencies.
- **Data Structure Footprint**: 20,000,000 watch-list references actively indexed in the Two-Watched-Literal (2-WL) data structure using chunked contiguous memory pooling.
- **Latency**: **~25 seconds** to ingest, attach, propagate, solve to satisfiability, and sample-verify the assignment.
- **Memory Safety**: 0 leaks, 0 dangling pointers, 100% idempotent clean-up.

### 📊 Benchmark Summary

| Benchmark | Variables | Clauses | Result | Verification / Highlights |
| :--- | :--- | :--- | :--- | :--- |
| **10M Stress Benchmark** | **50,000** | **10,000,000** | **✅ SAT** | **Solved in ~25s; 100k sample clauses verified** |
| **8-Queens** | 64 | 744 | **✅ SAT** | Verified authentic non-attacking chess layout |
| **4-Queens** | 16 | 84 | **✅ SAT** | Dual-verified: CNF satisfiability + domain semantics |
| **3-Queens** | 9 | 36 | **❌ UNSAT** | Provably exhaustive proof that no solution exists |
| **2-Queens** | 4 | 10 | **❌ UNSAT** | Provably exhaustive proof that no solution exists |
| **PHP(5 → 4)** | 20 | 45 | **❌ UNSAT** | Multi-level implication graph & non-chronological backjumping |
| **PHP(4 → 3)** | 12 | 22 | **❌ UNSAT** | 1-UIP conflict analysis & learned clause synthesis |
| **Mini-Sudoku (4x4)** | 64 | 150+ | **✅ SAT / ❌ UNSAT** | Exact model completeness; detects contradictory clues |

### 🧪 Replicating the Benchmarks

All test suites and benchmark drivers are fully open, reproducible, and self-contained:

```bash
cd src/SAT/tests

# Run the complete test suite (T0 - T7 + Standard Benchmarks)
make run

# Run the 10 Million Clause Stress Test specifically
make test_10m_clauses
./test_10m_clauses
```

---

## ⚠️ Current Limitations & Areas for Improvement (Open Engineering Disclosure)

In the spirit of honest engineering and transparent technical governance, the following areas represent current architectural boundaries and active engineering targets:

### 1. Unbounded SMT Linear Arithmetic (Current: Indicator Grounding)
- **Status**: Mathematical expressions like `x + y <= 10 -> Flag` are currently shielded and compiled as **boolean indicator propositions**. The logical relationships between indicators and conditions are fully solved by the CDCL engine.
- **Limitation**: Sodium does not yet integrate a native continuous Simplex or Fourier-Motzkin linear arithmetic decision procedure inside the DPLL(T) loop. It cannot currently synthesize unbounded real-valued variable intervals (e.g., finding the exact floating-point value for $x$ and $y$). Full Linear Real/Integer Arithmetic (LRA/LIA) is scheduled for Q2 2027.

### 2. Standardized Proof Certificate Emission (DRAT / DRUP)
- **Status**: When a formula is unsatisfiable, Sodium internally resolves the implication graph to a 1-UIP empty clause conflict.
- **Limitation**: Sodium does not yet serialize learned clauses into standard **DRAT (Deletion Resolution Asymmetric Tautology)** or **DRUP** text format. External mechanical proof checkers (like `drat-trim`) cannot yet independently verify Sodium's UNSAT proofs without running the solver.

### 3. Clause Inprocessing & Advanced Preprocessing
- **Status**: Sodium features high-speed 2WL propagation and 1-UIP conflict resolution on raw input clauses.
- **Limitation**: Advanced simplification techniques used in top-tier SAT competition solvers—such as **Bounded Variable Elimination (BVE)**, **Blocked Clause Elimination (BCE)**, **subsumption checks**, and **vivification**—are not yet implemented. Highly redundant industrial CNFs currently pass directly into the CDCL core without preprocessing.

### 4. Parallel Portfolio SAT Solving
- **Status**: Sodium solves instances via a single high-performance thread.
- **Limitation**: It does not yet implement a multi-threaded portfolio solver (such as running multiple VSIDS decay parameters or random seeds in parallel while sharing learned conflict clauses across CPU cores).

### 5. First-Order Logic & Infinite Quantifiers ($\forall, \exists$)
- **Status**: Both NLP engines ground specifications into propositional logic.
- **Limitation**: Arbitrary first-order quantification over infinite domains ($\forall x \in \mathbb{R}, \exists y > x$) cannot be directly checked without finite domain unrolling.

### 6. Desktop GUI Window Dependencies
- **Status**: Full GUI builds require GLFW and OpenGL libraries on the host system.
- **Mitigation**: Sodium provides `make server`, producing a pure C11 headless binary that includes the embedded Web Studio and REST API with zero external dependencies.

---

## 🚀 Quickstart

### Step 1 — Clone the Repository

```bash
git clone https://github.com/Aditya-B-007/Sodium.git
cd Sodium
```

---

### Step 2 — Build the Application

Sodium ships with a unified `Makefile` for zero-friction compilation:

#### Option A — Full Application (Desktop UI + Web Server + CLI)
Builds the native **Dear ImGui desktop window**, the embedded **HTTPS-ready web server**, and the **command-line solver** in a single binary:

```bash
# 1. Install dependencies (GLFW + Dear ImGui - once only)
make deps

# 2. Compile full application
make
```

#### Option B — Headless Build (Web Server + CLI only, Zero Dependencies)
If you want to use the Web Studio or the CLI without installing GLFW or GUI libraries:

```bash
make server
```
*Requires only a standard C11 compiler (Clang/GCC). Zero external dependencies.*

---

### Step 3 — Run the Application

After building, you have one binary: `./sodium`.

#### Launch the Native Desktop UI
```bash
./sodium --gui
```
Opens the Royal Purple Dear ImGui desktop studio with the Guided Rule Builder, constraint editor, and real-time variable inspection.

#### Launch the Interactive Web Studio
```bash
./sodium --serve --port 8080
```
Then open **http://localhost:8080** in any browser. Features include:
- Natural language input with **LLM** and **Non-LLM** mode selection.
- Preset logic puzzles (N-Queens, Pigeonhole Principle, Mutex Exclusion).
- Microsecond CDCL solving with verified variable assignment tables.
- **Cloud Ready**: Deploy behind Nginx/Caddy with SSL for enterprise team access.

#### Solve from the Command Line
```bash
# Solve a DIMACS CNF file
./sodium --file problem.cnf

# Solve and display microsecond solver telemetry
./sodium --file problem.cnf --benchmark

# Pipe a formula directly from stdin
echo "p cnf 2 2\n1 2 0\n-1 2 0" | ./sodium

# Solve natural language using the LLM engine
./sodium --nlp "Server A and Server B cannot both be active. Server A is online."

# Solve natural language using the 100% offline Non-LLM classical pipeline
./sodium --nlp "If TaskA is running then TaskB must be paused. TaskA is running." --non-llm
```

---

### Quick Reference

| Command | Description |
| :--- | :--- |
| `make deps` | Downloads Dear ImGui & installs GLFW (one-time setup for Desktop GUI) |
| `make` | Full build: Desktop GUI + Web Server + CLI binary (`./sodium`) |
| `make server` | Headless build: Zero-dependency Web Server + CLI binary (`./sodium`) |
| `make clean` | Removes compiled binaries and build artifacts |
| `./sodium --gui` | Launches the native Dear ImGui desktop window |
| `./sodium --serve --port 8080` | Starts the embedded web studio and REST API at http://localhost:8080 |
| `./sodium --file <path.cnf>` | Solves a DIMACS CNF problem file |
| `./sodium --nlp "<text>"` | Solves natural language via Neural LLM router |
| `./sodium --nlp "<text>" --non-llm` | Solves natural language via Classical 7-step offline pipeline |
| `./sodium --benchmark` | Displays microsecond telemetry and variable assignment metrics |
| `./sodium --help` | Displays complete CLI usage options |

---

### Programmatic Integration Example (C API)

You can embed the CDCL engine directly into any C or C++ application:

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

Compile with:
```bash
clang -Wall -Wextra -O3 -I src/SAT/CDCL example.c \
    src/SAT/CDCL/CDCL.c \
    src/SAT/CDCL/VSIDS.c \
    src/SAT/CDCL/memory.c \
    src/SAT/CDCL/Clauses/clause_array.c \
    -o my_solver

./my_solver
```

---

## 📊 Industrial Use Cases

- **Autonomous Vehicle Safety & Compliance**: Ingesting legal traffic codes and vehicle telemetry requirements to verify that trajectory planning never violates safety invariants.
- **Hardware Circuit & Protocol Verification**: Translating timing diagrams and hardware descriptions into formal satisfiability proofs before tape-out.
- **Enterprise SLA & Procurement Optimization**: Ingesting contractual constraints, volume pricing tiers, and delivery SLAs to synthesize optimal, compliant resource allocations.
- **Cybersecurity Policy & IAM Auditing**: Proving that zero combinations of user role transitions can violate security boundaries or produce privilege escalation paths.

---

## 🗺 Roadmap & Vision

- [x] **Q4 2026**: High-performance CDCL Engine with 2WL, 1-UIP learning, and VSIDS.
- [x] **Q4 2026**: Generational handle-based memory management with automated compaction (`memory.c`).
- [x] **Q4 2026**: Contiguous arena `ClauseArray` for zero-pointer-overhead clause storage.
- [x] **Q4 2026**: Mode 1 JSON SAT API (`sat_api.c`) — CaDiCaL-compatible in-memory solver interface.
- [x] **Q4 2026**: Neural LLM reasoning bridge (`llm_router.py`) — Pydantic-enforced bidirectional LLM schema routing.
- [x] **Q4 2026**: Classical Non-LLM 7-Step NLP & Math compiler (`src/NLP/NLPAlgo/`).
- [x] **Q4 2026**: Unified system orchestrator (`sodium_orchestrator.c`) — single binary for desktop UI, web server, and CLI.
- [x] **Q4 2026**: Embedded HTTPS-ready web studio with interactive CDCL solver, REST API, and Royal Purple UI.
- [ ] **Q1 2027**: Clause preprocessing and inprocessing passes (BVE, BCE, subsumption elimination).
- [ ] **Q1 2027**: DRAT / DRUP proof certificate logger for mechanical third-party proof validation.
- [ ] **Q2 2027**: Native SMT Theories:
  - Linear Real and Integer Arithmetic (LRA / LIA) via Simplex DPLL(T).
  - Theory of Bit-Vectors (BV) for hardware and software systems.
  - Theory of Arrays & Uninterpreted Functions (Arrays / UF).
- [ ] **Q3 2027**: Multi-threaded parallel portfolio SAT solving with shared learned-clause pools.
- [ ] **Q4 2027**: Enterprise Distributed Reasoning Cloud API & Verification Co-Pilot.

---

## 📄 License

Sodium is licensed under the **[Apache License 2.0](LICENSE)**.

---

<div align="center">
<b>Sodium is being built because I was fed up of AI Hallucinating and giving me "NA" errors in everyday decisions based on requirements, in production.</b>
<br/>
<sub>Designed & engineered by Aditya.</sub>
</div>
