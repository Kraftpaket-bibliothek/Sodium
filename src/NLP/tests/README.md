# Sodium NLP & Grounding Engine — Testing & Verification Suite

This directory contains the automated test suites, matrix coordinate grounding tests, and end-to-end controlled natural language verification drivers for Sodium's Classical NLP Pipeline (`src/NLP/NLPAlgo/`).

All tests run via Python's standard `unittest` framework and interface with the native C11 CDCL solver via `ctypes` through `SodiumExecutionBridge`.

---

## 📋 Test Suites Overview (35 Tests Across 6 Modules)

| Test Module | Tests | Target Subsystems | Key Invariants Verified | Status |
| :--- | :---: | :--- | :--- | :---: |
| **`test_contract_and_bridge.py`** | 6 | `SodiumCNFRequest` & ctypes Bridge | Pydantic JSON schema validation, $0$-terminated clause enforcement, assumptions validation, ctypes memory lifecycle | **✅ PASS** |
| **`test_logical_pipeline.py`** | 12 | Parser, Lemmatizer, Tokenizer | Operator precedence (`IFF < IMPLIES < OR < AND < NOT`), copular negation (`is not`), parentheses, syntax rejection | **✅ PASS** |
| **`test_solver_integration.py`** | 3 | Full Pipeline $\to$ C CDCL Engine | Python compilation $\to$ C shared library execution, satisfying assignment extraction, contradictory rule rejection | **✅ PASS** |
| **`test_domain_matrix.py`** | 11 | `DomainAxisDetector` & `DynamicDomainMatrix` | Closed-form formula $\text{VarID}(r, c) = r \times \|C\| + c$, Max-Planck/Saarland Ex 31.1 exact IDs ($1..125$), $550$ grid axioms | **✅ PASS** |
| **`test_relational_nlp.py`** | 2 | Relational SAT Formulation | End-to-end MPI Saarland Ex 31.1 solve, verification of all $13$ clues against model, contradictory UNSAT proof | **✅ PASS** |
| **`test_space_habitat_nlp.py`** | 1 | 128-Sentence Natural Language Pipeline | Compilation of $128$ natural-language sentences, $1{,}326$ clauses, $341$ variables, microsecond CDCL solve, uniqueness proof | **✅ PASS** |

---

## 🧩 Relational Puzzles & Domain Matrix Grounding

### 1. Max-Planck-Institut / Saarland University Exercise 31.1 (`test_relational_nlp.py`)
- **Problem**: 5 houses standing in a row, each with a unique Color, Nationality, Pet, Food, and Beverage.
- **Formulation**:
  - $R = 25$ properties (5 categories of 5 attributes each).
  - $C = 5$ columns (House 1 through House 5).
  - Total variables: $25 \times 5 = 125$ variables.
  - Structural clauses: $550$ exact-one (At-Least-One + At-Most-One) grid clauses.
- **Results**:
  - Sodium's CDCL solver solves the instance in sub-millisecond latency.
  - Successfully satisfies all 13 puzzle rules:
    - **House 1**: White, Danish, Budgerigar, Strawberries, Tea
    - **House 2**: Green, British, Fish, Pizza, Beer
    - **House 3**: Blue, Swedish, Cat, Sushi, Water
    - **House 4**: Red, German, Horse, Pretzels, Milk
    - **House 5**: Yellow, Norwegian, Dog, Waffles, Coffee

### 2. Space Habitat Module Allocation (`test_space_habitat_nlp.py`)
- **Problem**: 5 docked habitat modules, each with a Colour, Role, Homeworld, Instrument, and Beverage.
- **End-to-End NLP Verification**:
  - **Input**: $128$ controlled natural-language sentences (e.g. `"Doctor in Module 2."`, `"Europa in Module 1 if and only if Amber in Module 1."`, `"Drone in Module 1 implies Cider in Module 2."`).
  - **NLP Pipeline**: Normalized by `LogicLemmatizer`, tokenized by `MathTokenizer`, parsed into ILR trees by `LogicalDependencyParser`, coordinate-grounded by `EntityResolver` + `DynamicDomainMatrix`, and converted to CNF by `LogicalToCNFCompiler`.
  - **Complexity**: $1{,}326$ clauses across $341$ variables ($125$ matrix coordinates + auxiliary Tseitin variables).
- **Solver Execution**:
  - Solved by Sodium in **$0.065\text{ ms}$**.
  - Verified **$100\%$ unique** (blocking clause returns `UNSATISFIABLE`).
  - **Decoded Grid**:
    - **Module 1**: Amber, Lieutenant, Europa, Compass, Cocoa
    - **Module 2**: Cobalt, Doctor, Titan, Scanner, Espresso
    - **Module 3**: Jade, Ensign, Ganymede, Drone, Electrolyte
    - **Module 4**: Crimson, Mechanic, Venus, Wrench, Cider
    - **Module 5**: Silver, Commander, Mars, Beacon, Matcha

---

## 🏃 Running the Python NLP Test Suite

Make sure the local virtual environment is active:

```bash
# Run the complete test suite (all 35 tests):
.venv/bin/python3 -m unittest discover src/NLP/tests

# Run individual test modules:
.venv/bin/python3 -m unittest src/NLP/tests/test_domain_matrix.py
.venv/bin/python3 -m unittest src/NLP/tests/test_relational_nlp.py
.venv/bin/python3 -m unittest src/NLP/tests/test_space_habitat_nlp.py
.venv/bin/python3 -m unittest src/NLP/tests/test_logical_pipeline.py
.venv/bin/python3 -m unittest src/NLP/tests/test_contract_and_bridge.py
.venv/bin/python3 -m unittest src/NLP/tests/test_solver_integration.py
```
