/**
 * ============================================================================
 * Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
 * Module: SAT / CDCL / Tests
 * File: src/SAT/tests/test_electronic_board.c
 * Description: Electronic Board Logic Verification & Hardware Miter Test Suite.
 *
 * Verifies two industry-standard digital electronic hardware architectures:
 *
 * 1. Industrial Functional Safety & Power Interlock Board:
 *    - Modeled after IEC 61508 / ISO 13849 Category 4 Safety Interlock systems.
 *    - Quad-rail window supervisory circuits (+3.3V, +5V, +12V, -12V UVLO/OVLO).
 *    - Dual-channel redundant Emergency Stop loop with cross-monitoring.
 *    - Thermal trip latch and over-current sensor.
 *    - Redundant actuator relay drive interlocks.
 *    - Formal Safety Verification: Proves hazardous power-up during fault is UNSAT.
 *    - Diagnostic Verification: Proves channel discrepancy trips lockout (SAT).
 *
 * 2. Texas Instruments SN74181 4-Bit Arithmetic Logic Unit (ALU):
 *    - Industry-standard 63-gate MSI arithmetic & logic benchmark.
 *    - Combinational Equivalence Checking (CEC): Miter circuit proves 74181
 *      gate netlist is mathematically identical to golden 4-bit addition (UNSAT).
 *    - Logic Mode Verification: Proves bitwise XOR logic operation matches spec (UNSAT).
 *    - Automatic Test Pattern Generation (ATPG): Solves for exact input test
 *      vector that exposes an internal stuck-at physical defect (SAT).
 * ============================================================================
 */

#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================= */
/* 1. Circuit Construction & Tseitin Transformation Infrastructure           */
/* ========================================================================= */

typedef struct {
    CDCLSolver* solver;
    int         next_var;
} BoardCircuit;

static inline void circuit_init(BoardCircuit* bc, CDCLSolver* solver) {
    memset(solver, 0, sizeof(*solver));
    clause_array_init(&solver->arena, 0);
    bc->solver   = solver;
    bc->next_var = 1;
}

static inline int new_var(BoardCircuit* bc) {
    return bc->next_var++;
}

static inline void add_clause1(BoardCircuit* bc, int a) {
    int lits[1] = {a};
    clause_array_allocate(&bc->solver->arena, lits, 1, false, 0.0f);
}

static inline void add_clause2(BoardCircuit* bc, int a, int b) {
    int lits[2] = {a, b};
    clause_array_allocate(&bc->solver->arena, lits, 2, false, 0.0f);
}

static inline void add_clause3(BoardCircuit* bc, int a, int b, int c) {
    int lits[3] = {a, b, c};
    clause_array_allocate(&bc->solver->arena, lits, 3, false, 0.0f);
}

static inline void add_clause4(BoardCircuit* bc, int a, int b, int c, int d) {
    int lits[4] = {a, b, c, d};
    clause_array_allocate(&bc->solver->arena, lits, 4, false, 0.0f);
}

static inline void add_clause_n(BoardCircuit* bc, const int* lits, int n) {
    clause_array_allocate(&bc->solver->arena, lits, n, false, 0.0f);
}

/* Gate-level Tseitin Encoders */

static inline int gate_not(BoardCircuit* bc, int in) {
    int out = new_var(bc);
    add_clause2(bc, -in, -out);
    add_clause2(bc, in, out);
    return out;
}

static inline int gate_and2(BoardCircuit* bc, int a, int b) {
    int out = new_var(bc);
    add_clause3(bc, -a, -b, out);
    add_clause2(bc, a, -out);
    add_clause2(bc, b, -out);
    return out;
}

static inline int gate_or2(BoardCircuit* bc, int a, int b) {
    int out = new_var(bc);
    add_clause3(bc, a, b, -out);
    add_clause2(bc, -a, out);
    add_clause2(bc, -b, out);
    return out;
}

static inline int gate_xor2(BoardCircuit* bc, int a, int b) {
    int out = new_var(bc);
    add_clause3(bc, -a, -b, -out);
    add_clause3(bc, a, b, -out);
    add_clause3(bc, a, -b, out);
    add_clause3(bc, -a, b, out);
    return out;
}

static inline int gate_and3(BoardCircuit* bc, int a, int b, int c) {
    int in[3] = {a, b, c};
    int out = new_var(bc);
    add_clause4(bc, -a, -b, -c, out);
    add_clause2(bc, in[0], -out);
    add_clause2(bc, in[1], -out);
    add_clause2(bc, in[2], -out);
    return out;
}

static inline int gate_or3(BoardCircuit* bc, int a, int b, int c) {
    int in[3] = {a, b, c};
    int out = new_var(bc);
    add_clause4(bc, a, b, c, -out);
    add_clause2(bc, -in[0], out);
    add_clause2(bc, -in[1], out);
    add_clause2(bc, -in[2], out);
    return out;
}

static inline int gate_and_n(BoardCircuit* bc, const int* in, int n) {
    int out = new_var(bc);
    int* lits = (int*)malloc((size_t)(n + 1) * sizeof(int));
    for (int i = 0; i < n; i++) {
        lits[i] = -in[i];
        add_clause2(bc, in[i], -out);
    }
    lits[n] = out;
    add_clause_n(bc, lits, n + 1);
    free(lits);
    return out;
}

static inline int gate_or_n(BoardCircuit* bc, const int* in, int n) {
    int out = new_var(bc);
    int* lits = (int*)malloc((size_t)(n + 1) * sizeof(int));
    for (int i = 0; i < n; i++) {
        lits[i] = in[i];
        add_clause2(bc, -in[i], out);
    }
    lits[n] = -out;
    add_clause_n(bc, lits, n + 1);
    free(lits);
    return out;
}

/* ========================================================================= */
/* 2. Industrial Safety & Power Interlock Board Netlist                      */
/* ========================================================================= */

typedef struct {
    /* Inputs */
    int v3p3_ok;
    int v5p0_ok;
    int v12p0_ok;
    int v12n0_ok;
    int estop_ch1;
    int estop_ch2;
    int thermal_trip;
    int overcurrent_fault;
    int mcu_watchdog_ok;

    /* Internal Diagnostic Nodes */
    int power_healthy;
    int estop_discrepancy;
    int estop_active;
    int safety_clear;
    int relay_en1;
    int relay_en2;

    /* Primary Interlock Output */
    int load_power_en;
} SafetyBoardNetlist;

static SafetyBoardNetlist build_safety_board(BoardCircuit* bc) {
    SafetyBoardNetlist sb;

    /* Allocate Inputs */
    sb.v3p3_ok           = new_var(bc);
    sb.v5p0_ok           = new_var(bc);
    sb.v12p0_ok          = new_var(bc);
    sb.v12n0_ok          = new_var(bc);
    sb.estop_ch1         = new_var(bc);
    sb.estop_ch2         = new_var(bc);
    sb.thermal_trip      = new_var(bc);
    sb.overcurrent_fault = new_var(bc);
    sb.mcu_watchdog_ok   = new_var(bc);

    /* 1. Quad-rail Window Supervisory */
    int rails[4] = {sb.v3p3_ok, sb.v5p0_ok, sb.v12p0_ok, sb.v12n0_ok};
    sb.power_healthy = gate_and_n(bc, rails, 4);

    /* 2. Dual-Channel E-Stop & Discrepancy Detection */
    sb.estop_discrepancy = gate_xor2(bc, sb.estop_ch1, sb.estop_ch2);
    int no_discrepancy   = gate_not(bc, sb.estop_discrepancy);
    int estop_both       = gate_and2(bc, sb.estop_ch1, sb.estop_ch2);
    sb.estop_active      = gate_and2(bc, estop_both, no_discrepancy);

    /* 3. Thermal and Overcurrent Protection */
    int not_thermal     = gate_not(bc, sb.thermal_trip);
    int not_overcurrent = gate_not(bc, sb.overcurrent_fault);
    sb.safety_clear     = gate_and2(bc, not_thermal, not_overcurrent);

    /* 4. Redundant Actuator Relay Drive Interlocks */
    int ch1_terms[4] = {sb.power_healthy, sb.estop_active, sb.safety_clear, sb.mcu_watchdog_ok};
    sb.relay_en1 = gate_and_n(bc, ch1_terms, 4);

    int ch2_terms[4] = {sb.power_healthy, sb.estop_active, sb.safety_clear, sb.mcu_watchdog_ok};
    sb.relay_en2 = gate_and_n(bc, ch2_terms, 4);

    /* 5. Dual Series Contacts Actuator Power Delivery */
    sb.load_power_en = gate_and2(bc, sb.relay_en1, sb.relay_en2);

    return sb;
}

/* ========================================================================= */
/* 3. Texas Instruments SN74181 4-Bit ALU Netlist                            */
/* ========================================================================= */

typedef struct {
    int a[4];      /**< 4-bit operand A */
    int b[4];      /**< 4-bit operand B */
    int s[4];      /**< 4-bit function select S[3:0] */
    int m;         /**< Mode select (0 = Arithmetic, 1 = Logic) */
    int cin;       /**< Carry-in input */

    int f[4];      /**< 4-bit function output F[3:0] */
    int cout;      /**< High-speed lookahead carry out */
    int p_terms[4];/**< Propagate intermediate terms */
    int g_terms[4];/**< Generate intermediate terms */
} Alu74181Netlist;

/**
 * @brief Constructs gate-level 74181 ALU following TI SN74181 specifications.
 * @param bc         Board circuit context.
 * @param a_bus      Input vector A[3:0] (pass NULL to allocate new).
 * @param b_bus      Input vector B[3:0] (pass NULL to allocate new).
 * @param s_bus      Function select S[3:0] (pass NULL to allocate new).
 * @param m_line     Mode select line (pass 0 to allocate new).
 * @param cin_line   Carry input (pass 0 to allocate new).
 * @param fault_net  Optional stuck-at-0 net index (-1 for defect-free).
 */
static Alu74181Netlist build_74181_alu(BoardCircuit* bc,
                                      const int* a_bus,
                                      const int* b_bus,
                                      const int* s_bus,
                                      int m_line,
                                      int cin_line,
                                      int fault_net) {
    Alu74181Netlist alu;

    for (int i = 0; i < 4; i++) {
        alu.a[i] = a_bus ? a_bus[i] : new_var(bc);
        alu.b[i] = b_bus ? b_bus[i] : new_var(bc);
        alu.s[i] = s_bus ? s_bus[i] : new_var(bc);
    }
    alu.m   = m_line ? m_line : new_var(bc);
    alu.cin = cin_line ? cin_line : new_var(bc);

    /* Generate Propagate (P) and Generate (G) terms per bit */
    for (int i = 0; i < 4; i++) {
        int not_b = gate_not(bc, alu.b[i]);

        int p_term1 = gate_and2(bc, alu.b[i], alu.s[0]);
        int p_term2 = gate_and2(bc, not_b, alu.s[1]);
        int p_or    = gate_or2(bc, p_term1, p_term2);
        alu.p_terms[i] = gate_or2(bc, alu.a[i], p_or);

        int g_term1 = gate_and3(bc, alu.a[i], not_b, alu.s[2]);
        int g_term2 = gate_and3(bc, alu.a[i], alu.b[i], alu.s[3]);
        alu.g_terms[i] = gate_or2(bc, g_term1, g_term2);
    }

    /* Effective G terms for downstream carry chain (with ATPG fault injection support) */
    int g_eff[4];
    for (int i = 0; i < 4; i++) {
        if (fault_net == i) {
            /* Fault wire is disconnected from driver and held stuck-at-0 */
            int stuck_zero = new_var(bc);
            add_clause1(bc, -stuck_zero);
            g_eff[i] = stuck_zero;
        } else {
            g_eff[i] = alu.g_terms[i];
        }
    }

    /* High-Speed Carry-Lookahead Generator */
    int not_m = gate_not(bc, alu.m); /* Carries enabled only when M = 0 */

    int c[4];
    c[0] = gate_and2(bc, alu.cin, not_m);

    /* C1 = G0 | (P0 & C0) */
    int c1_prop = gate_and2(bc, alu.p_terms[0], c[0]);
    c[1] = gate_and2(bc, gate_or2(bc, g_eff[0], c1_prop), not_m);

    /* C2 = G1 | (P1 & G0) | (P1 & P0 & C0) */
    int c2_t1 = g_eff[1];
    int c2_t2 = gate_and2(bc, alu.p_terms[1], g_eff[0]);
    int c2_t3 = gate_and3(bc, alu.p_terms[1], alu.p_terms[0], c[0]);
    c[2] = gate_and2(bc, gate_or3(bc, c2_t1, c2_t2, c2_t3), not_m);

    /* C3 = G2 | (P2 & G1) | (P2 & P1 & G0) | (P2 & P1 & P0 & C0) */
    int c3_t1 = g_eff[2];
    int c3_t2 = gate_and2(bc, alu.p_terms[2], g_eff[1]);
    int c3_t3 = gate_and3(bc, alu.p_terms[2], alu.p_terms[1], g_eff[0]);
    int c3_t4_terms[4] = {alu.p_terms[2], alu.p_terms[1], alu.p_terms[0], c[0]};
    int c3_t4 = gate_and_n(bc, c3_t4_terms, 4);
    int c3_or[4] = {c3_t1, c3_t2, c3_t3, c3_t4};
    c[3] = gate_and2(bc, gate_or_n(bc, c3_or, 4), not_m);

    /* Cout (C4) */
    int c4_t1 = g_eff[3];
    int c4_t2 = gate_and2(bc, alu.p_terms[3], g_eff[2]);
    int c4_t3 = gate_and3(bc, alu.p_terms[3], alu.p_terms[2], g_eff[1]);
    int c4_t4_terms[4] = {alu.p_terms[3], alu.p_terms[2], alu.p_terms[1], g_eff[0]};
    int c4_t4 = gate_and_n(bc, c4_t4_terms, 4);
    int c4_t5_terms[5] = {alu.p_terms[3], alu.p_terms[2], alu.p_terms[1], alu.p_terms[0], c[0]};
    int c4_t5 = gate_and_n(bc, c4_t5_terms, 5);
    int c4_or[5] = {c4_t1, c4_t2, c4_t3, c4_t4, c4_t5};
    alu.cout = gate_and2(bc, gate_or_n(bc, c4_or, 5), not_m);

    /* Primary Function Outputs F[i] = (P_i ^ G_eff[i]) ^ C_i */
    for (int i = 0; i < 4; i++) {
        int pg_xor = gate_xor2(bc, alu.p_terms[i], g_eff[i]);
        alu.f[i]   = gate_xor2(bc, pg_xor, c[i]);
    }

    return alu;
}

/* ========================================================================= */
/* 4. Golden 4-Bit Arithmetic Reference Netlist                              */
/* ========================================================================= */

typedef struct {
    int sum[4];
    int cout;
} GoldenAdderNetlist;

static GoldenAdderNetlist build_golden_adder(BoardCircuit* bc,
                                             const int* a,
                                             const int* b,
                                             int cin) {
    GoldenAdderNetlist ga;
    int c = cin;

    for (int i = 0; i < 4; i++) {
        int ab_xor = gate_xor2(bc, a[i], b[i]);
        ga.sum[i]  = gate_xor2(bc, ab_xor, c);

        /* c_next = (a & b) | (c & ab_xor) */
        int ab_and  = gate_and2(bc, a[i], b[i]);
        int c_prop  = gate_and2(bc, c, ab_xor);
        c = gate_or2(bc, ab_and, c_prop);
    }
    ga.cout = c;
    return ga;
}

/* ========================================================================= */
/* 5. Test Cases: Electronic Safety & Functional Interlock Board             */
/* ========================================================================= */

/* ── Test 1: Functional Safe Boot (Witness Generation) ── */
static void test_board_interlock_safe_boot_sat(void) {
    CDCLSolver solver;
    BoardCircuit bc;
    circuit_init(&bc, &solver);

    SafetyBoardNetlist board = build_safety_board(&bc);

    /* Normal healthy boot conditions:
     * - All 4 power rails in spec
     * - Dual E-Stops closed (1)
     * - No thermal trip (0)
     * - No overcurrent fault (0)
     * - MCU Watchdog heartbeat healthy (1)
     */
    add_clause1(&bc, board.v3p3_ok);
    add_clause1(&bc, board.v5p0_ok);
    add_clause1(&bc, board.v12p0_ok);
    add_clause1(&bc, board.v12n0_ok);
    add_clause1(&bc, board.estop_ch1);
    add_clause1(&bc, board.estop_ch2);
    add_clause1(&bc, -board.thermal_trip);
    add_clause1(&bc, -board.overcurrent_fault);
    add_clause1(&bc, board.mcu_watchdog_ok);

    /* Assert that actuator power can safely engage */
    add_clause1(&bc, board.load_power_en);

    SolverMode mode = SOLVE_ARENA(&solver, bc.next_var - 1);

    TEST_ASSERT(mode == SAT, "Healthy safety board must be SATISFIABLE and enable load power");
    TEST_ASSERT(solver.assignment[board.load_power_en] == TRUE, "load_power_en must be TRUE");
    TEST_ASSERT(solver.assignment[board.relay_en1] == TRUE, "relay_en1 must be TRUE");
    TEST_ASSERT(solver.assignment[board.relay_en2] == TRUE, "relay_en2 must be TRUE");
    TEST_ASSERT(solver.assignment[board.power_healthy] == TRUE, "power_healthy must be TRUE");

    DESTROY_SOLVER(&solver);
}

/* ── Test 2: Hazard Prevention Invariant Proof (Formal Verification) ── */
static void test_board_interlock_hazard_prevention_unsat(void) {
    CDCLSolver solver;
    BoardCircuit bc;
    circuit_init(&bc, &solver);

    SafetyBoardNetlist board = build_safety_board(&bc);

    /* Inject Hazard: 12V gate driver rail drops below UVLO lockout threshold (v12p0_ok = 0) */
    add_clause1(&bc, -board.v12p0_ok);

    /* All other sensors healthy */
    add_clause1(&bc, board.v3p3_ok);
    add_clause1(&bc, board.v5p0_ok);
    add_clause1(&bc, board.v12n0_ok);
    add_clause1(&bc, board.estop_ch1);
    add_clause1(&bc, board.estop_ch2);
    add_clause1(&bc, -board.thermal_trip);
    add_clause1(&bc, -board.overcurrent_fault);
    add_clause1(&bc, board.mcu_watchdog_ok);

    /* Attacker / Fault Hypothesis: Can the actuator power relay still energize? */
    add_clause1(&bc, board.load_power_en);

    SolverMode mode = SOLVE_ARENA(&solver, bc.next_var - 1);

    TEST_ASSERT(mode == UNSAT, "Hazard condition must be mathematically UNSATISFIABLE");

    DESTROY_SOLVER(&solver);
}

/* ── Test 3: Redundant Discrepancy Fault Detection ── */
static void test_board_redundancy_discrepancy_trip_sat(void) {
    CDCLSolver solver;
    BoardCircuit bc;
    circuit_init(&bc, &solver);

    SafetyBoardNetlist board = build_safety_board(&bc);

    /* Discrepancy condition: E-Stop 1 closed, E-Stop 2 open (contact bounce / wire fault) */
    add_clause1(&bc, board.estop_ch1);
    add_clause1(&bc, -board.estop_ch2);

    /* Solver derives state */
    SolverMode mode = SOLVE_ARENA(&solver, bc.next_var - 1);

    TEST_ASSERT(mode == SAT, "Discrepancy condition must resolve SATISFIABLE");
    TEST_ASSERT(solver.assignment[board.estop_discrepancy] == TRUE, "estop_discrepancy must be flagged TRUE");
    TEST_ASSERT(solver.assignment[board.estop_active] == FALSE, "estop_active must be de-asserted FALSE");
    TEST_ASSERT(solver.assignment[board.load_power_en] == FALSE, "load_power_en must be safely de-energized FALSE");

    DESTROY_SOLVER(&solver);
}

/* ========================================================================= */
/* 6. Test Cases: TI SN74181 ALU Combinational Equivalence & ATPG            */
/* ========================================================================= */

/* ── Test 4: 74181 Arithmetic Addition Miter (Formal CEC Proof) ── */
static void test_74181_alu_addition_miter_unsat(void) {
    CDCLSolver solver;
    BoardCircuit bc;
    circuit_init(&bc, &solver);

    /* Common input buses */
    int a[4], b[4], s[4];
    for (int i = 0; i < 4; i++) {
        a[i] = new_var(&bc);
        b[i] = new_var(&bc);
        s[i] = new_var(&bc);
    }
    int m   = new_var(&bc);
    int cin = new_var(&bc);

    /* 74181 ALU in arithmetic addition mode:
     * Mode M = 0 (Arithmetic)
     * Function S = [1, 0, 0, 1] (S0=1, S1=0, S2=0, S3=1 for F = A + B)
     * Cin = 0
     */
    add_clause1(&bc, -m);
    add_clause1(&bc, s[0]);
    add_clause1(&bc, -s[1]);
    add_clause1(&bc, -s[2]);
    add_clause1(&bc, s[3]);
    add_clause1(&bc, -cin);

    /* Instantiate 74181 Gate Netlist */
    Alu74181Netlist alu = build_74181_alu(&bc, a, b, s, m, cin, -1);

    /* Instantiate Golden Reference 4-bit Carry Lookahead Adder */
    GoldenAdderNetlist golden = build_golden_adder(&bc, a, b, cin);

    /* Combinational Equivalence Miter:
     * Output difference: diff = (F[0] ^ Sum[0]) | ... | (F[3] ^ Sum[3]) | (Cout_alu ^ Cout_golden)
     */
    int diffs[5];
    for (int i = 0; i < 4; i++) {
        diffs[i] = gate_xor2(&bc, alu.f[i], golden.sum[i]);
    }
    diffs[4] = gate_xor2(&bc, alu.cout, golden.cout);

    int miter_diff = gate_or_n(&bc, diffs, 5);

    /* Assert that a functional difference exists */
    add_clause1(&bc, miter_diff);

    /* Prove that no discrepancy can exist across all 2^8 = 256 input combinations */
    SolverMode mode = SOLVE_ARENA(&solver, bc.next_var - 1);

    TEST_ASSERT(mode == UNSAT,
                "74181 ALU gate-level addition must be formally EQUIVALENT to Golden Adder (UNSAT)");

    DESTROY_SOLVER(&solver);
}

/* ── Test 5: 74181 Bitwise Logic Mode Equivalence Miter ── */
static void test_74181_alu_logic_modes_miter_unsat(void) {
    CDCLSolver solver;
    BoardCircuit bc;
    circuit_init(&bc, &solver);

    int a[4], b[4], s[4];
    for (int i = 0; i < 4; i++) {
        a[i] = new_var(&bc);
        b[i] = new_var(&bc);
        s[i] = new_var(&bc);
    }
    int m   = new_var(&bc);
    int cin = new_var(&bc);

    /* Logic Mode: M = 1, S = 1001 (XOR mode: F = A ^ B) */
    add_clause1(&bc, m);      /* M = 1 (Logic) */
    add_clause1(&bc, s[0]);   /* S0 = 1 */
    add_clause1(&bc, -s[1]);  /* S1 = 0 */
    add_clause1(&bc, -s[2]);  /* S2 = 0 */
    add_clause1(&bc, s[3]);   /* S3 = 1 */
    add_clause1(&bc, -cin);

    Alu74181Netlist alu = build_74181_alu(&bc, a, b, s, m, cin, -1);

    /* Golden Reference: Bitwise XOR gates */
    int golden_xor[4];
    for (int i = 0; i < 4; i++) {
        golden_xor[i] = gate_xor2(&bc, a[i], b[i]);
    }

    int diffs[4];
    for (int i = 0; i < 4; i++) {
        diffs[i] = gate_xor2(&bc, alu.f[i], golden_xor[i]);
    }
    int miter_diff = gate_or_n(&bc, diffs, 4);

    /* Assert difference */
    add_clause1(&bc, miter_diff);

    SolverMode mode = SOLVE_ARENA(&solver, bc.next_var - 1);

    TEST_ASSERT(mode == UNSAT, "74181 XOR logic mode must be formally EQUIVALENT to bitwise XOR (UNSAT)");

    DESTROY_SOLVER(&solver);
}

/* ── Test 6: Automatic Test Pattern Generation (ATPG) for Internal Defect ── */
static void test_74181_alu_atpg_stuck_at_fault_sat(void) {
    CDCLSolver solver;
    BoardCircuit bc;
    circuit_init(&bc, &solver);

    int a[4], b[4], s[4];
    for (int i = 0; i < 4; i++) {
        a[i] = new_var(&bc);
        b[i] = new_var(&bc);
        s[i] = new_var(&bc);
    }
    int m   = new_var(&bc);
    int cin = new_var(&bc);

    /* Arithmetic mode addition */
    add_clause1(&bc, -m);
    add_clause1(&bc, s[0]);
    add_clause1(&bc, -s[1]);
    add_clause1(&bc, -s[2]);
    add_clause1(&bc, s[3]);
    add_clause1(&bc, -cin);

    /* Golden 74181 instance */
    Alu74181Netlist golden_alu = build_74181_alu(&bc, a, b, s, m, cin, -1);

    /* Defective 74181 instance: internal carry generation net G1 stuck-at-0 */
    Alu74181Netlist faulty_alu = build_74181_alu(&bc, a, b, s, m, cin, 1);

    /* Fault-Sensitization Miter: Output diff between golden and faulty IC */
    int diffs[5];
    for (int i = 0; i < 4; i++) {
        diffs[i] = gate_xor2(&bc, golden_alu.f[i], faulty_alu.f[i]);
    }
    diffs[4] = gate_xor2(&bc, golden_alu.cout, faulty_alu.cout);
    int fault_detected = gate_or_n(&bc, diffs, 5);

    /* Assert that test vector exposes the fault at primary pins */
    add_clause1(&bc, fault_detected);

    SolverMode mode = SOLVE_ARENA(&solver, bc.next_var - 1);

    TEST_ASSERT(mode == SAT, "ATPG must find a valid test vector sensitizing the stuck-at fault (SAT)");

    /* Verify the test vector synthesized by the CDCL solver */
    TEST_ASSERT(solver.assignment[fault_detected] == TRUE, "fault_detected must be TRUE in synthesized model");

    /* Confirm that operand bit 1 is sensitized (A1=1, B1=1 generates carry G1) */
    bool sensitized = (solver.assignment[a[1]] == TRUE && solver.assignment[b[1]] == TRUE);
    TEST_ASSERT(sensitized, "Synthesized ATPG vector must sensitize generate net G1 (A1=1, B1=1)");

    DESTROY_SOLVER(&solver);
}

/* ========================================================================= */
/* 7. Test Suite Entry Point                                                 */
/* ========================================================================= */

int main(void) {
    printf("=== Electronic Board Logic Verification & Miter Test Suite ===\n");
    RUN_TEST(test_board_interlock_safe_boot_sat);
    RUN_TEST(test_board_interlock_hazard_prevention_unsat);
    RUN_TEST(test_board_redundancy_discrepancy_trip_sat);
    RUN_TEST(test_74181_alu_addition_miter_unsat);
    RUN_TEST(test_74181_alu_logic_modes_miter_unsat);
    RUN_TEST(test_74181_alu_atpg_stuck_at_fault_sat);
    REPORT_AND_EXIT();
}
