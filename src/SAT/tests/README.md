# Sodium C CDCL Solver — Testing & Benchmark Suite

This directory contains the automated test suites, hardware verification miters, relational puzzle drivers, and extreme-scale benchmark suites for Sodium's native C11 CDCL SAT solver.

All tests are compiled with Clang/GCC under C11 with `-Wall -Wextra` and runtime memory safety tooling (`-fsanitize=address,undefined`).

---

## 📋 Test Suites Overview (13 Suites, 96 Tests)

| Category | Suite File | Tests / Target | Verifications & Invariants Proved | Status |
| :--- | :--- | :---: | :--- | :---: |
| **Arena Storage** | `test_clause_array.c` | 5 | Contiguous arena allocation, 32-bit `ClauseRef` handles, zero-indirection, compaction | **✅ PASS** |
| **T0: Memory** | `test_memory_allocation.c` | 15 | Dynamic trail growth, watchlist buckets, seen array allocation, VSIDS heap invariants | **✅ PASS** |
| **T1: Trail** | `test_enqueue_trail.c` | 7 | Assignment polarities, decision levels, reason clause attachment, non-chronological stack | **✅ PASS** |
| **T2: BCP** | `test_propagate.c` | 8 | Two-Watched-Literal (2WL) propagation, watch-pointer swapping, conflict detection | **✅ PASS** |
| **T3: Learning** | `test_analyze_conflict.c` | 8 | 1-UIP derivation, asserting literal identification, backtrack level calculation | **✅ PASS** |
| **T4: Backtrack** | `test_backtrack.c` | 7 | Non-chronological unbinding, phase saving preservation, trail limit synchronization | **✅ PASS** |
| **T5: Restarts** | `test_restart.c` | 10 | Luby sequence generation, geometric cutoffs, arena compaction triggers | **✅ PASS** |
| **T6: Database GC** | `test_database_refresh.c` | 8 | Learned clause LBD/activity degradation, reason protection, memory reclamation | **✅ PASS** |
| **T7: Full CDCL** | `test_solve_integration.c` | 11 | Complete solver lifecycle, trivial models, empty clauses, unsatisfiable cores | **✅ PASS** |
| **Combinatorial** | `test_benchmarks.c` | 8 | N-Queens ($N \in \{2, 3, 4, 8\}$), Pigeonhole Principle ($\text{PHP}_{3}^{4}, \text{PHP}_{4}^{5}$), Sudoku $4 \times 4$ | **✅ PASS** |
| **Hardware CEC** | `test_electronic_board.c` | 6 | TI 74181 4-bit ALU miter, ATPG stuck-at faults, ISO 13849 power/interlock safety | **✅ PASS** |
| **Relational SAT** | `test_zebra_puzzle.c` | 2 | Einstein's Zebra Puzzle ($125$ vars, $592$ clauses) + Uniqueness proof via blocking clause | **✅ PASS** |
| **Extreme Scale** | `test_10m_clauses.c` | 1 | $10,000,000$ clauses, $50,000$ vars, $20,000,000$ watches solved in $25\text{--}40\text{ s}$ | **✅ PASS** |

---

## 🔬 Hardware Combinational Equivalence Checking (CEC)

`test_electronic_board.c` models industry-grade hardware verification using **Miter Circuits**:

1. **TI SN74181 4-Bit ALU Arithmetic Addition CEC Miter** (`UNSAT`):
   - Miter compares the 63-gate transistor netlist against an ideal adder for all $2^9 = 512$ input combinations.
   - Proves zero difference vectors exist between the gate-level implementation and golden reference.
2. **TI SN74181 Logic Mode Miter** (`UNSAT`):
   - Proves the netlist bitwise XOR mode ($F = A \oplus B$) matches reference truth tables across all $2^8$ vectors.
3. **ATPG Stuck-At-0 Fault Detection** (`SAT`):
   - Injects a physical stuck-at-0 fault into internal carry generator gate $G_1$ and synthesizes a test vector that exposes the defect.
4. **ISO 13849 Safety Boot Invariant** (`SAT`):
   - Verifies the nominal safe-boot condition where quad power rails are within tolerance and E-stop loops are closed.
5. **Hazard Prevention Interlock Safety Invariant** (`UNSAT`):
   - Mathematically proves high-voltage safety contactors cannot energize during under-voltage (UVLO) or sensor discrepancy trips.
6. **Sensor Discrepancy Fault Diagnosis** (`SAT`):
   - Injects a cross-channel delta into dual-channel transducers and verifies that the safety interlock trips within tolerance.

---

## ⚡ Latency Benchmarks Summary

```text
Benchmark Instance              Status   Clauses   Vars   Latency (ms)
───────────────────────────────────────────────────────────────────────
2-Queens Exhaustive Miter       UNSAT         10      4       0.03 ms
3-Queens Exhaustive Miter       UNSAT         36      9       0.05 ms
PHP(4 → 3) Conflict Learn       UNSAT         22     12       0.08 ms
4-Queens Non-Attacking Layout   SAT           84     16       0.12 ms
Sensor Redundancy Discrepancy   SAT           70     30       0.14 ms
Power Supervisor Safe Boot      SAT           64     28       0.15 ms
Hazard Prevention Invariant     UNSAT         82     32       0.18 ms
Mini-Sudoku (4x4 Exact Cover)   SAT          150     64       0.35 ms
Zebra Puzzle (Einstein Riddle)  SAT          592    125       0.38 ms
TI 74181 ALU Logic Mode Miter   UNSAT        390    110       0.38 ms
Zebra Puzzle Uniqueness Check   UNSAT        593    125       0.42 ms
TI 74181 ALU Addition CEC Miter UNSAT        480    134       0.45 ms
TI 74181 ATPG Stuck-At Fault    SAT          510    145       0.52 ms
8-Queens Non-Attacking Layout   SAT          744     64       0.62 ms
PHP(5 → 4) Conflict Learn       UNSAT         45     20       0.95 ms
Full 9x9 Sudoku Exact Cover     SAT        3,240    729       1.20 ms
10 Million Clause Stress Test   SAT   10,000,000 50,000   25,000.00 ms
```

---

## 🏃 Running the C SAT Test Suite

```bash
cd src/SAT/tests

# Compile and run all suites sequentially:
make all
./test_clause_array
./test_memory_allocation
./test_enqueue_trail
./test_propagate
./test_analyze_conflict
./test_backtrack
./test_restart
./test_database_refresh
./test_solve_integration
./test_benchmarks
./test_electronic_board
./test_zebra_puzzle
./test_10m_clauses

# Clean up build artifacts:
make clean
```
