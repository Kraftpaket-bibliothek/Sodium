/**
 * ============================================================================
 * Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
 * Module: SAT / CDCL / Tests
 * File: src/SAT/tests/test_zebra_puzzle.c
 * Description: Classic Zebra Puzzle (Einstein's Riddle) CDCL SAT Verification.
 * ============================================================================
 */

#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

enum Category {
    CAT_COLOR = 0,
    CAT_NATION,
    CAT_DRINK,
    CAT_SMOKE,
    CAT_PET,
    NUM_CATEGORIES
};

enum Color {
    COL_RED = 0,
    COL_GREEN,
    COL_WHITE,
    COL_YELLOW,
    COL_BLUE
};

enum Nation {
    NAT_ENGLISHMAN = 0,
    NAT_SPANIARD,
    NAT_UKRAINIAN,
    NAT_NORWEGIAN,
    NAT_JAPANESE
};

enum Drink {
    DRK_COFFEE = 0,
    DRK_TEA,
    DRK_MILK,
    DRK_ORANGE_JUICE,
    DRK_WATER
};

enum Smoke {
    SMK_OLD_GOLD = 0,
    SMK_KOOLS,
    SMK_CHESTERFIELDS,
    SMK_LUCKY_STRIKE,
    SMK_PARLIAMENTS
};

enum Pet {
    PET_DOG = 0,
    PET_SNAILS,
    PET_FOX,
    PET_HORSE,
    PET_ZEBRA
};

static const char* COLOR_NAMES[5]  = {"Red", "Green", "White", "Yellow", "Blue"};
static const char* NATION_NAMES[5] = {"Englishman", "Spaniard", "Ukrainian", "Norwegian", "Japanese"};
static const char* DRINK_NAMES[5]  = {"Coffee", "Tea", "Milk", "Orange Juice", "Water"};
static const char* SMOKE_NAMES[5]  = {"Old Gold", "Kools", "Chesterfields", "Lucky Strike", "Parliaments"};
static const char* PET_NAMES[5]    = {"Dog", "Snails", "Fox", "Horse", "Zebra"};

/* Variable indexing: (category * 5 + attribute) * 5 + house [1..5] */
static inline int z_var(int cat, int attr, int house) {
    return (cat * 5 + attr) * 5 + house;
}

static inline void add_c1(CDCLSolver* s, int a) {
    int lits[1] = {a};
    clause_array_allocate(&s->arena, lits, 1, false, 0.0f);
}

static inline void add_c2(CDCLSolver* s, int a, int b) {
    int lits[2] = {a, b};
    clause_array_allocate(&s->arena, lits, 2, false, 0.0f);
}

static inline void add_c3(CDCLSolver* s, int a, int b, int c) {
    int lits[3] = {a, b, c};
    clause_array_allocate(&s->arena, lits, 3, false, 0.0f);
}

static inline void add_c5(CDCLSolver* s, int a, int b, int c, int d, int e) {
    int lits[5] = {a, b, c, d, e};
    clause_array_allocate(&s->arena, lits, 5, false, 0.0f);
}

static void add_same_house(CDCLSolver* s, int c1, int a1, int c2, int a2) {
    for (int h = 1; h <= 5; h++) {
        add_c2(s, -z_var(c1, a1, h), z_var(c2, a2, h));
        add_c2(s, -z_var(c2, a2, h), z_var(c1, a1, h));
    }
}

static void add_immediately_right(CDCLSolver* s, int c_right, int a_right, int c_left, int a_left) {
    /* Right cannot be in house 1, Left cannot be in house 5 */
    add_c1(s, -z_var(c_right, a_right, 1));
    add_c1(s, -z_var(c_left, a_left, 5));
    for (int h = 1; h <= 4; h++) {
        add_c2(s, -z_var(c_left, a_left, h), z_var(c_right, a_right, h + 1));
        add_c2(s, -z_var(c_right, a_right, h + 1), z_var(c_left, a_left, h));
    }
}

static void add_next_to(CDCLSolver* s, int c1, int a1, int c2, int a2) {
    /* If at house 1, other must be at house 2 */
    add_c2(s, -z_var(c1, a1, 1), z_var(c2, a2, 2));
    /* If at house 5, other must be at house 4 */
    add_c2(s, -z_var(c1, a1, 5), z_var(c2, a2, 4));
    /* Middle houses */
    for (int h = 2; h <= 4; h++) {
        add_c3(s, -z_var(c1, a1, h), z_var(c2, a2, h - 1), z_var(c2, a2, h + 1));
    }
    /* Symmetric */
    add_c2(s, -z_var(c2, a2, 1), z_var(c1, a1, 2));
    add_c2(s, -z_var(c2, a2, 5), z_var(c1, a1, 4));
    for (int h = 2; h <= 4; h++) {
        add_c3(s, -z_var(c2, a2, h), z_var(c1, a1, h - 1), z_var(c1, a1, h + 1));
    }
}

static void build_zebra_puzzle(CDCLSolver* s) {
    memset(s, 0, sizeof(*s));
    clause_array_init(&s->arena, 0);

    /* 1. Exactly one house per entity (25 entities) */
    for (int c = 0; c < NUM_CATEGORIES; c++) {
        for (int a = 0; a < 5; a++) {
            /* At least one house */
            add_c5(s, z_var(c, a, 1), z_var(c, a, 2), z_var(c, a, 3), z_var(c, a, 4), z_var(c, a, 5));
            /* At most one house */
            for (int h1 = 1; h1 <= 5; h1++) {
                for (int h2 = h1 + 1; h2 <= 5; h2++) {
                    add_c2(s, -z_var(c, a, h1), -z_var(c, a, h2));
                }
            }
        }
    }

    /* 2. In each category, each house has exactly one entity */
    for (int c = 0; c < NUM_CATEGORIES; c++) {
        for (int h = 1; h <= 5; h++) {
            /* At least one attribute */
            add_c5(s, z_var(c, 0, h), z_var(c, 1, h), z_var(c, 2, h), z_var(c, 3, h), z_var(c, 4, h));
            /* At most one attribute */
            for (int a1 = 0; a1 < 5; a1++) {
                for (int a2 = a1 + 1; a2 < 5; a2++) {
                    add_c2(s, -z_var(c, a1, h), -z_var(c, a2, h));
                }
            }
        }
    }

    /* 3. The 14 Standard Clues */
    /* Clue 1: The Englishman lives in the red house */
    add_same_house(s, CAT_NATION, NAT_ENGLISHMAN, CAT_COLOR, COL_RED);

    /* Clue 2: The Spaniard owns the dog */
    add_same_house(s, CAT_NATION, NAT_SPANIARD, CAT_PET, PET_DOG);

    /* Clue 3: Coffee is drunk in the green house */
    add_same_house(s, CAT_DRINK, DRK_COFFEE, CAT_COLOR, COL_GREEN);

    /* Clue 4: The Ukrainian drinks tea */
    add_same_house(s, CAT_NATION, NAT_UKRAINIAN, CAT_DRINK, DRK_TEA);

    /* Clue 5: The green house is immediately to the right of the ivory (white) house */
    add_immediately_right(s, CAT_COLOR, COL_GREEN, CAT_COLOR, COL_WHITE);

    /* Clue 6: The Old Gold smoker owns snails */
    add_same_house(s, CAT_SMOKE, SMK_OLD_GOLD, CAT_PET, PET_SNAILS);

    /* Clue 7: Kools are smoked in the yellow house */
    add_same_house(s, CAT_SMOKE, SMK_KOOLS, CAT_COLOR, COL_YELLOW);

    /* Clue 8: Milk is drunk in the middle house (House 3) */
    add_c1(s, z_var(CAT_DRINK, DRK_MILK, 3));

    /* Clue 9: The Norwegian lives in the first house (House 1) */
    add_c1(s, z_var(CAT_NATION, NAT_NORWEGIAN, 1));

    /* Clue 10: The man who smokes Chesterfields lives in the house next to the man with the fox */
    add_next_to(s, CAT_SMOKE, SMK_CHESTERFIELDS, CAT_PET, PET_FOX);

    /* Clue 11: Kools are smoked in the house next to the house where the horse is kept */
    add_next_to(s, CAT_SMOKE, SMK_KOOLS, CAT_PET, PET_HORSE);

    /* Clue 12: The Lucky Strike smoker drinks orange juice */
    add_same_house(s, CAT_SMOKE, SMK_LUCKY_STRIKE, CAT_DRINK, DRK_ORANGE_JUICE);

    /* Clue 13: The Japanese smokes Parliaments */
    add_same_house(s, CAT_NATION, NAT_JAPANESE, CAT_SMOKE, SMK_PARLIAMENTS);

    /* Clue 14: The Norwegian lives next to the blue house */
    add_next_to(s, CAT_NATION, NAT_NORWEGIAN, CAT_COLOR, COL_BLUE);
}

static void test_zebra_puzzle_solve(void) {
    CDCLSolver solver;
    build_zebra_puzzle(&solver);

    int total_vars = 125;
    SolverMode mode = SOLVE_ARENA(&solver, total_vars);

    TEST_ASSERT(mode == SAT, "Zebra Puzzle must be SATISFIABLE");

    /* Decode assignments */
    int solution[NUM_CATEGORIES][6]; /* house 1..5 */
    memset(solution, -1, sizeof(solution));

    for (int c = 0; c < NUM_CATEGORIES; c++) {
        for (int a = 0; a < 5; a++) {
            for (int h = 1; h <= 5; h++) {
                int v = z_var(c, a, h);
                if (solver.assignment[v] == TRUE) {
                    solution[c][h] = a;
                }
            }
        }
    }

    /* Print CDCL Decoded Solution Table */
    printf("\n");
    printf("+-----------------------------------------------------------------------------------------+\n");
    printf("|                             CDCL SOLVER OUTPUT: ZEBRA PUZZLE                            |\n");
    printf("+-------+------------+---------------+--------------+---------------+---------------------+\n");
    printf("| House | Color      | Nationality   | Drink        | Smoke         | Pet                 |\n");
    printf("+-------+------------+---------------+--------------+---------------+---------------------+\n");
    for (int h = 1; h <= 5; h++) {
        printf("|   %d   | %-10s | %-13s | %-12s | %-13s | %-19s |\n",
               h,
               COLOR_NAMES[solution[CAT_COLOR][h]],
               NATION_NAMES[solution[CAT_NATION][h]],
               DRINK_NAMES[solution[CAT_DRINK][h]],
               SMOKE_NAMES[solution[CAT_SMOKE][h]],
               PET_NAMES[solution[CAT_PET][h]]);
    }
    printf("+-------+------------+---------------+--------------+---------------+---------------------+\n");

    /* Formally Verify Crucial Deductions */
    /* Q1: Who drinks water? */
    int water_house = -1;
    for (int h = 1; h <= 5; h++) {
        if (solution[CAT_DRINK][h] == DRK_WATER) {
            water_house = h;
            break;
        }
    }
    printf(">> DEDUCTION 1: Water is drunk in House %d by the %s.\n",
           water_house, NATION_NAMES[solution[CAT_NATION][water_house]]);
    TEST_ASSERT(water_house == 1, "Water must be in House 1");
    TEST_ASSERT(solution[CAT_NATION][water_house] == NAT_NORWEGIAN, "Norwegian drinks water");

    /* Q2: Who owns the zebra? */
    int zebra_house = -1;
    for (int h = 1; h <= 5; h++) {
        if (solution[CAT_PET][h] == PET_ZEBRA) {
            zebra_house = h;
            break;
        }
    }
    printf(">> DEDUCTION 2: The Zebra is kept in House %d by the %s.\n",
           zebra_house, NATION_NAMES[solution[CAT_NATION][zebra_house]]);
    TEST_ASSERT(zebra_house == 5, "Zebra must be in House 5");
    TEST_ASSERT(solution[CAT_NATION][zebra_house] == NAT_JAPANESE, "Japanese owns the zebra");

    /* Also assert complete house profile matches standard known solution */
    /* House 1: Yellow, Norwegian, Water, Kools, Fox */
    TEST_ASSERT(solution[CAT_COLOR][1] == COL_YELLOW, "House 1 Color = Yellow");
    TEST_ASSERT(solution[CAT_NATION][1] == NAT_NORWEGIAN, "House 1 Nation = Norwegian");
    TEST_ASSERT(solution[CAT_DRINK][1] == DRK_WATER, "House 1 Drink = Water");
    TEST_ASSERT(solution[CAT_SMOKE][1] == SMK_KOOLS, "House 1 Smoke = Kools");
    TEST_ASSERT(solution[CAT_PET][1] == PET_FOX, "House 1 Pet = Fox");

    /* House 2: Blue, Ukrainian, Tea, Chesterfields, Horse */
    TEST_ASSERT(solution[CAT_COLOR][2] == COL_BLUE, "House 2 Color = Blue");
    TEST_ASSERT(solution[CAT_NATION][2] == NAT_UKRAINIAN, "House 2 Nation = Ukrainian");
    TEST_ASSERT(solution[CAT_DRINK][2] == DRK_TEA, "House 2 Drink = Tea");
    TEST_ASSERT(solution[CAT_SMOKE][2] == SMK_CHESTERFIELDS, "House 2 Smoke = Chesterfields");
    TEST_ASSERT(solution[CAT_PET][2] == PET_HORSE, "House 2 Pet = Horse");

    /* House 3: Red, Englishman, Milk, Old Gold, Snails */
    TEST_ASSERT(solution[CAT_COLOR][3] == COL_RED, "House 3 Color = Red");
    TEST_ASSERT(solution[CAT_NATION][3] == NAT_ENGLISHMAN, "House 3 Nation = Englishman");
    TEST_ASSERT(solution[CAT_DRINK][3] == DRK_MILK, "House 3 Drink = Milk");
    TEST_ASSERT(solution[CAT_SMOKE][3] == SMK_OLD_GOLD, "House 3 Smoke = Old Gold");
    TEST_ASSERT(solution[CAT_PET][3] == PET_SNAILS, "House 3 Pet = Snails");

    /* House 4: White/Ivory, Spaniard, Orange Juice, Lucky Strike, Dog */
    TEST_ASSERT(solution[CAT_COLOR][4] == COL_WHITE, "House 4 Color = White");
    TEST_ASSERT(solution[CAT_NATION][4] == NAT_SPANIARD, "House 4 Nation = Spaniard");
    TEST_ASSERT(solution[CAT_DRINK][4] == DRK_ORANGE_JUICE, "House 4 Drink = Orange Juice");
    TEST_ASSERT(solution[CAT_SMOKE][4] == SMK_LUCKY_STRIKE, "House 4 Smoke = Lucky Strike");
    TEST_ASSERT(solution[CAT_PET][4] == PET_DOG, "House 4 Pet = Dog");

    /* House 5: Green, Japanese, Coffee, Parliaments, Zebra */
    TEST_ASSERT(solution[CAT_COLOR][5] == COL_GREEN, "House 5 Color = Green");
    TEST_ASSERT(solution[CAT_NATION][5] == NAT_JAPANESE, "House 5 Nation = Japanese");
    TEST_ASSERT(solution[CAT_DRINK][5] == DRK_COFFEE, "House 5 Drink = Coffee");
    TEST_ASSERT(solution[CAT_SMOKE][5] == SMK_PARLIAMENTS, "House 5 Smoke = Parliaments");
    TEST_ASSERT(solution[CAT_PET][5] == PET_ZEBRA, "House 5 Pet = Zebra");

    DESTROY_SOLVER(&solver);
}

static void test_zebra_puzzle_uniqueness(void) {
    /* Verify solution is UNIQUE: Assert NOT(solution) and verify UNSAT */
    CDCLSolver solver;
    build_zebra_puzzle(&solver);

    /* Construct clause blocking the known unique solution:
     * To block a solution, we add a clause with the negation of all 25 true assignments.
     */
    int blocking_clause[25];
    int idx = 0;
    blocking_clause[idx++] = -z_var(CAT_COLOR, COL_YELLOW, 1);
    blocking_clause[idx++] = -z_var(CAT_NATION, NAT_NORWEGIAN, 1);
    blocking_clause[idx++] = -z_var(CAT_DRINK, DRK_WATER, 1);
    blocking_clause[idx++] = -z_var(CAT_SMOKE, SMK_KOOLS, 1);
    blocking_clause[idx++] = -z_var(CAT_PET, PET_FOX, 1);

    blocking_clause[idx++] = -z_var(CAT_COLOR, COL_BLUE, 2);
    blocking_clause[idx++] = -z_var(CAT_NATION, NAT_UKRAINIAN, 2);
    blocking_clause[idx++] = -z_var(CAT_DRINK, DRK_TEA, 2);
    blocking_clause[idx++] = -z_var(CAT_SMOKE, SMK_CHESTERFIELDS, 2);
    blocking_clause[idx++] = -z_var(CAT_PET, PET_HORSE, 2);

    blocking_clause[idx++] = -z_var(CAT_COLOR, COL_RED, 3);
    blocking_clause[idx++] = -z_var(CAT_NATION, NAT_ENGLISHMAN, 3);
    blocking_clause[idx++] = -z_var(CAT_DRINK, DRK_MILK, 3);
    blocking_clause[idx++] = -z_var(CAT_SMOKE, SMK_OLD_GOLD, 3);
    blocking_clause[idx++] = -z_var(CAT_PET, PET_SNAILS, 3);

    blocking_clause[idx++] = -z_var(CAT_COLOR, COL_WHITE, 4);
    blocking_clause[idx++] = -z_var(CAT_NATION, NAT_SPANIARD, 4);
    blocking_clause[idx++] = -z_var(CAT_DRINK, DRK_ORANGE_JUICE, 4);
    blocking_clause[idx++] = -z_var(CAT_SMOKE, SMK_LUCKY_STRIKE, 4);
    blocking_clause[idx++] = -z_var(CAT_PET, PET_DOG, 4);

    blocking_clause[idx++] = -z_var(CAT_COLOR, COL_GREEN, 5);
    blocking_clause[idx++] = -z_var(CAT_NATION, NAT_JAPANESE, 5);
    blocking_clause[idx++] = -z_var(CAT_DRINK, DRK_COFFEE, 5);
    blocking_clause[idx++] = -z_var(CAT_SMOKE, SMK_PARLIAMENTS, 5);
    blocking_clause[idx++] = -z_var(CAT_PET, PET_ZEBRA, 5);

    clause_array_allocate(&solver.arena, blocking_clause, 25, false, 0.0f);

    SolverMode mode = SOLVE_ARENA(&solver, 125);
    TEST_ASSERT(mode == UNSAT, "Zebra Puzzle must have exactly 1 unique solution (Blocking clause makes it UNSAT)");

    DESTROY_SOLVER(&solver);
}

int main(void) {
    printf("=== Zebra's Puzzle CDCL Verification & Uniqueness Test Suite ===\n");
    RUN_TEST(test_zebra_puzzle_solve);
    RUN_TEST(test_zebra_puzzle_uniqueness);
    REPORT_AND_EXIT();
}
