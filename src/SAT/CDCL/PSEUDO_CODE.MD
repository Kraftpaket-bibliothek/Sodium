# Sodium CDCL Engine: Architectural Pseudocode

This document outlines the detailed pseudocode and mechanics for the three foundational modules comprising the core CDCL solver in `src/SAT/CDCL`:
1. **Memory Manager (`memory.c` / `memory.h`)**: Generational handle-based record allocator with compaction and defragmentation.
2. **VSIDS Engine (`VSIDS.c` / `VSIDS.h`)**: Variable State Independent Decaying Sum decision heuristic backed by `MemoryManager`.
3. **CDCL Engine (`CDCL.c` / `CDCL.h`)**: Core SAT solver with Two-Watched Literals (2WL), 1-UIP conflict analysis, clause learning, and non-chronological backjumping.

---

```
+-----------------------------------------------------------------------------+
|                                CDCL Engine                                  |
|                                 (CDCL.c)                                    |
|                                                                             |
|  - 2-Watched Literals BCP (Propagate)                                       |
|  - 1-UIP Conflict Analysis (Analyze)                                        |
|  - Non-Chronological Backtracking (Backtrack)                               |
|  - Main Search Loop (Solve)                                                 |
+------------------------------------+----------------------------------------+
                                     |
                                     v
                       +---------------------------+
                       |       VSIDS Engine        |
                       |         (VSIDS.c)         |
                       |                           |
                       |  - Bump on conflict       |
                       |  - Exponential decay      |
                       |  - Max-activity selection |
                       |  - Float rescale guard    |
                       +-------------+-------------+
                                     |
                                     v
                       +---------------------------+
                       |      Memory Manager       |
                       |        (memory.c)         |
                       |                           |
                       |  - Generational handles   |
                       |  - Buffer slot management |
                       |  - In-place compaction    |
                       |  - ABA / stale prevention |
                       +---------------------------+
```

---

## 1. Memory Manager Module (`memory.c`)

### 1.1 Data Structures

```
RECORD RecordHeader:
    handle_index  : Integer (Index in handle table)
    generation    : Integer (Handle lifecycle generation)
    data_size     : Integer (Number of active data elements)
    data_capacity : Integer (Total allocated capacity for this record)
    is_deleted    : Boolean (Tombstone flag)

UNION Slot:
    record_header : RecordHeader
    data_pointer  : Pointer (void*)

RECORD HandleEntry:
    buffer_offset : Integer (Slot offset in buffer, or INVALID_OFFSET)
    generation    : Integer (Current generation counter)
    is_active     : Boolean (True if handle points to an active record)

RECORD RecordHandle:
    index         : Integer (Handle table index)
    generation    : Integer (Generation at allocation time)

RECORD MemoryManager:
    buffer                : Array of Slot
    buffer_capacity       : Integer
    used_slots            : Integer
    handle_table          : Array of HandleEntry
    handle_table_capacity : Integer
    next_unused_handle    : Integer
    free_handles          : Stack of Integer
    wasted_slots          : Integer
    compaction_threshold  : Float (e.g. 0.30 = 30% wasted space)
```

### 1.2 Core Procedures

#### `InitializeMemoryManager(initial_slots) -> Manager`
```
FUNCTION InitializeMemoryManager(initial_slots):
    Allocate Manager
    Manager.buffer = Allocate Array of Slot of size initial_slots
    Manager.handle_table = Allocate Array of HandleEntry of size initial_slots
    
    FOR i = 0 TO initial_slots - 1 DO:
        Manager.handle_table[i].buffer_offset = INVALID_OFFSET
        Manager.handle_table[i].generation = 1
        Manager.handle_table[i].is_active = FALSE
    END FOR

    Manager.buffer_capacity = initial_slots
    Manager.used_slots = 0
    Manager.handle_table_capacity = initial_slots
    Manager.next_unused_handle = 0
    Manager.free_handles = Empty Stack
    Manager.wasted_slots = 0
    Manager.compaction_threshold = 0.30

    RETURN Manager
END FUNCTION
```

#### `ValidateHandle(Manager, Handle) -> (Status, Offset)`
```
FUNCTION ValidateHandle(Manager, Handle):
    IF Handle.index >= Manager.handle_table_capacity THEN:
        RETURN (STATUS_INVALID, 0)
    END IF

    entry = Manager.handle_table[Handle.index]
    IF NOT entry.is_active OR entry.generation != Handle.generation THEN:
        RETURN (STATUS_INVALID, 0)  // Stale or freed handle (solves ABA problem)
    END IF

    offset = entry.buffer_offset
    IF offset >= Manager.used_slots THEN:
        RETURN (STATUS_INVALID, 0)
    END IF

    header = Manager.buffer[offset].record_header
    IF header.is_deleted OR header.generation != Handle.generation THEN:
        RETURN (STATUS_INVALID, 0)
    END IF

    RETURN (STATUS_SUCCESS, offset)
END FUNCTION
```

#### `AllocateRecord(Manager, data_items, data_length) -> (Status, Handle)`
```
FUNCTION AllocateRecord(Manager, data_items, data_length):
    required_slots = 1 + data_length  // 1 Slot for header + data_length slots
    EnsureBufferCapacity(Manager, Manager.used_slots + required_slots)

    // Reuse a recycled handle index if available, else take the next unused
    IF Manager.free_handles is not empty THEN:
        handle_index = Manager.free_handles.pop()
        generation = Manager.handle_table[handle_index].generation
    ELSE:
        handle_index = Manager.next_unused_handle++
        EnsureHandleTableCapacity(Manager, handle_index + 1)
        generation = 1
    END IF

    offset = Manager.used_slots

    // Write RecordHeader into buffer[offset]
    Manager.buffer[offset].record_header.handle_index = handle_index
    Manager.buffer[offset].record_header.generation = generation
    Manager.buffer[offset].record_header.data_size = data_length
    Manager.buffer[offset].record_header.data_capacity = data_length
    Manager.buffer[offset].record_header.is_deleted = FALSE

    // Copy data slots
    FOR i = 0 TO data_length - 1 DO:
        Manager.buffer[offset + 1 + i].data_pointer = data_items[i]
    END FOR

    // Register active handle in table
    Manager.handle_table[handle_index].buffer_offset = offset
    Manager.handle_table[handle_index].generation = generation
    Manager.handle_table[handle_index].is_active = TRUE

    Manager.used_slots += required_slots

    RETURN (STATUS_SUCCESS, RecordHandle(index: handle_index, generation: generation))
END FUNCTION
```

#### `DeleteRecord(Manager, Handle)`
```
FUNCTION DeleteRecord(Manager, Handle):
    (status, offset) = ValidateHandle(Manager, Handle)
    IF status != STATUS_SUCCESS THEN RETURN status

    header = Manager.buffer[offset].record_header
    slots_freed = 1 + header.data_capacity

    // Mark record deleted (tombstone)
    Manager.buffer[offset].record_header.is_deleted = TRUE

    // Invalidate handle entry and increment generation (prevents ABA resurrection)
    Manager.handle_table[Handle.index].is_active = FALSE
    Manager.handle_table[Handle.index].buffer_offset = INVALID_OFFSET
    Manager.handle_table[Handle.index].generation += 1

    // Recycle handle index onto free stack
    Manager.free_handles.push(Handle.index)
    Manager.wasted_slots += slots_freed

    // Trigger defragmentation if compaction threshold exceeded
    IF CompactionNeeded(Manager) THEN:
        CompactAndRemoveDeleted(Manager)
    END IF

    RETURN STATUS_SUCCESS
END FUNCTION
```

#### `CompactAndRemoveDeleted(Manager)`
```
FUNCTION CompactAndRemoveDeleted(Manager):
    read_cursor = 0
    write_cursor = 0

    WHILE read_cursor < Manager.used_slots DO:
        header = Manager.buffer[read_cursor].record_header
        record_slots = 1 + header.data_capacity

        IF header.is_deleted THEN:
            // Skip tombstoned slots
            read_cursor += record_slots
            CONTINUE
        END IF

        // Move alive record forward
        IF write_cursor != read_cursor THEN:
            CopySlots(from: read_cursor, to: write_cursor, count: record_slots)
        END IF

        // Update handle table to new compacted offset
        Manager.handle_table[header.handle_index].buffer_offset = write_cursor

        write_cursor += record_slots
        read_cursor += record_slots
    END WHILE

    Manager.used_slots = write_cursor
    Manager.wasted_slots = 0
END FUNCTION
```

---

## 2. VSIDS Heuristic Module (`VSIDS.c`)

### 2.1 Data Structures

```
RECORD VSIDS:
    manager         : Pointer to MemoryManager
    activity_handle : RecordHandle (Points to activity score array in MemoryManager)
    variable_count  : Integer
    increment       : Float (Current activity bump amount, starts at 1.0)
    decay_factor    : Float (Decay multiplier, typically 0.95)
    owns_manager    : Boolean (True if VSIDS allocated its own MemoryManager)
```

### 2.2 Core Procedures

#### `VSIDS_Initialize(VSIDS, Manager, variable_count, decay_factor)`
```
FUNCTION VSIDS_Initialize(VSIDS, Manager, variable_count, decay_factor):
    VSIDS.variable_count = variable_count
    VSIDS.increment = 1.0
    VSIDS.decay_factor = decay_factor

    IF Manager is NULL THEN:
        VSIDS.manager = InitializeMemoryManager(variable_count + 16)
        VSIDS.owns_manager = TRUE
    ELSE:
        VSIDS.manager = Manager
        VSIDS.owns_manager = FALSE
    END IF

    // Allocate record of size (variable_count + 1) filled with 0.0
    initial_scores = Array of size (variable_count + 1) initialized to DoubleToSlot(0.0)
    AllocateRecord(VSIDS.manager, initial_scores, variable_count + 1, OUT VSIDS.activity_handle)
END FUNCTION
```

#### `VSIDS_Bump(VSIDS, variable)`
```
FUNCTION VSIDS_Bump(VSIDS, variable):
    IF variable < 1 OR variable > VSIDS.variable_count THEN RETURN

    offset = ValidateHandle(VSIDS.manager, VSIDS.activity_handle)
    current_score = SlotToDouble(VSIDS.manager.buffer[offset + 1 + variable])
    
    new_score = current_score + VSIDS.increment
    VSIDS.manager.buffer[offset + 1 + variable] = DoubleToSlot(new_score)

    // Rescale if score exceeds floating-point threshold (e.g. 1e100)
    IF new_score >= 1e100 THEN:
        VSIDS_Rescale(VSIDS)
    END IF
END FUNCTION
```

#### `VSIDS_Decay(VSIDS)`
```
FUNCTION VSIDS_Decay(VSIDS):
    // Division of increment is mathematically equivalent to decaying all scores by 0.95:
    // score / decay == score * (1 / 0.95)
    VSIDS.increment /= VSIDS.decay_factor

    IF VSIDS.increment >= 1e100 THEN:
        VSIDS_Rescale(VSIDS)
    END IF
END FUNCTION
```

#### `VSIDS_Rescale(VSIDS)`
```
FUNCTION VSIDS_Rescale(VSIDS):
    offset = ValidateHandle(VSIDS.manager, VSIDS.activity_handle)
    FOR v = 1 TO VSIDS.variable_count DO:
        val = SlotToDouble(VSIDS.manager.buffer[offset + 1 + v])
        val /= 1e100
        VSIDS.manager.buffer[offset + 1 + v] = DoubleToSlot(val)
    END FOR

    VSIDS.increment /= 1e100
END FUNCTION
```

#### `VSIDS_SelectVariable(VSIDS, assignment) -> Variable`
```
FUNCTION VSIDS_SelectVariable(VSIDS, assignment):
    offset = ValidateHandle(VSIDS.manager, VSIDS.activity_handle)
    best_variable = NO_VARIABLE (0)
    best_score = -INFINITY

    FOR v = 1 TO VSIDS.variable_count DO:
        IF assignment[v] == UNASSIGNED THEN:
            score = SlotToDouble(VSIDS.manager.buffer[offset + 1 + v])
            IF score > best_score THEN:
                best_score = score
                best_variable = v
            END IF
        END IF
    END FOR

    RETURN best_variable
END FUNCTION
```

#### `VSIDS_Cleanup(VSIDS)`
```
FUNCTION VSIDS_Cleanup(VSIDS):
    DeleteRecord(VSIDS.manager, VSIDS.activity_handle)
    IF VSIDS.owns_manager THEN:
        DestroyMemoryManager(VSIDS.manager)
    END IF
END FUNCTION
```

---

## 3. CDCL Engine Module (`CDCL.c`)

### 3.1 Data Structures

```
RECORD Clause:
    lits       : Array of Integer (Literals in clause)
    size       : Integer
    is_learned : Boolean

RECORD CDCLSolver:
    number_of_variables : Integer
    memory_manager      : Pointer to MemoryManager
    
    // Trail & Assignment State
    assignment          : Array of Integer (1..N: UNASSIGNED, VAL_TRUE, VAL_FALSE)
    decision_level      : Array of Integer (1..N: Level at which var was assigned)
    reason              : Array of Pointer to Clause (Antecedent implication clause)
    trail               : Dynamic Vector of Integer (Chronological assigned literals)
    trail_limits        : Dynamic Vector of Integer (Trail index checkpoints for levels)
    current_level       : Integer
    propagation_head    : Integer (Queue cursor in trail)
    
    // Watch Lists (Two-Watched Literals)
    watch_lists         : Array of ClauseVector (Indexed by LIT_TO_INDEX)
    clauses             : Dynamic Vector of Clause
    
    // Decision Heuristic
    vsids               : VSIDS
    
    // Conflict Analysis Scratchpad
    seen                : Array of Boolean (Flags for 1-UIP traversal)
    bump_variables      : Dynamic Vector of Integer
```

### 3.2 Core Procedures

#### `Enqueue(Solver, literal, reason_clause) -> Boolean`
```
FUNCTION Enqueue(Solver, literal, reason_clause):
    var = ABS(literal)
    val = (literal > 0) ? VAL_TRUE : VAL_FALSE

    // Check consistency if already assigned
    IF Solver.assignment[var] != UNASSIGNED THEN:
        RETURN (Solver.assignment[var] == val)
    END IF

    // Assign variable
    Solver.assignment[var] = val
    Solver.decision_level[var] = Solver.current_level
    Solver.reason[var] = reason_clause
    Solver.trail.push(literal)

    RETURN TRUE
END FUNCTION
```

#### `Backtrack(Solver, target_level)`
```
FUNCTION Backtrack(Solver, target_level):
    WHILE Solver.current_level > target_level DO:
        trail_start = Solver.trail_limits.pop()

        // Unassign all variables down to the start of target_level
        WHILE Solver.trail.size > trail_start DO:
            literal = Solver.trail.pop()
            var = ABS(literal)

            Solver.assignment[var] = UNASSIGNED
            Solver.decision_level[var] = 0
            Solver.reason[var] = NULL
        END WHILE

        Solver.current_level--
    END WHILE

    IF Solver.propagation_head > Solver.trail.size THEN:
        Solver.propagation_head = Solver.trail.size
    END IF
END FUNCTION
```

#### `Propagate(Solver) -> ConflictClause or NULL` (Two-Watched Literals)
```
FUNCTION Propagate(Solver):
    WHILE Solver.propagation_head < Solver.trail.size DO:
        assigned_lit = Solver.trail[Solver.propagation_head++]
        false_lit = -assigned_lit  // The literal that was falsified

        watch_list = Solver.watch_lists[LIT_TO_INDEX(false_lit)]

        i = 0
        j = 0

        WHILE i < watch_list.size DO:
            clause = watch_list[i]

            // Normalize: ensure false_lit is at clause.lits[0]
            IF clause.lits[1] == false_lit THEN:
                SWAP(clause.lits[0], clause.lits[1])
            END IF

            other_watch = clause.lits[1]

            // 1. If other watched literal is already TRUE, clause is satisfied
            IF LitValue(Solver, other_watch) == VAL_TRUE THEN:
                watch_list[j++] = watch_list[i++]
                CONTINUE
            END IF

            // 2. Scan remaining literals for a non-FALSE replacement
            found_replacement = FALSE
            FOR k = 2 TO clause.size - 1 DO:
                IF LitValue(Solver, clause.lits[k]) != VAL_FALSE THEN:
                    found_replacement = TRUE
                    clause.lits[0] = clause.lits[k]
                    clause.lits[k] = false_lit

                    // Move clause to new watch list
                    Solver.watch_lists[LIT_TO_INDEX(clause.lits[0])].push(clause)
                    BREAK
                END IF
            END FOR

            IF found_replacement THEN:
                i++  // Unlink from current watch list
                CONTINUE
            END IF

            // 3. No replacement found: keep watcher in current list
            watch_list[j++] = watch_list[i++]

            // 4. Check status of other watched literal
            IF LitValue(Solver, other_watch) == VAL_FALSE THEN:
                // Both watched literals are FALSE -> CONFLICT!
                CopyRemainingWatches(watch_list, from: i, to: j)
                watch_list.size = j
                RETURN clause  // Return conflicting clause
            ELSE:
                // other_watch is UNASSIGNED -> UNIT PROPAGATION!
                IF NOT Enqueue(Solver, other_watch, clause) THEN:
                    CopyRemainingWatches(watch_list, from: i, to: j)
                    watch_list.size = j
                    RETURN clause  // Conflict during enqueue
                END IF
            END IF
        END WHILE

        watch_list.size = j
    END WHILE

    RETURN NULL  // No conflicts detected
END FUNCTION
```

#### `Analyze(Solver, conflict_clause) -> (LearnedClause, BacktrackLevel)` (1-UIP Scheme)
```
FUNCTION Analyze(Solver, conflict_clause):
    learned_clause = Empty List
    learned_clause.push(0)  // Reserve index 0 for the asserting 1-UIP literal

    Solver.bump_variables.clear()
    path_count = 0
    trail_index = Solver.trail.size - 1
    clause_to_process = conflict_clause
    pivot_variable = NO_VARIABLE

    WHILE TRUE DO:
        FOR each lit IN clause_to_process.lits DO:
            var = ABS(lit)
            IF var == pivot_variable THEN CONTINUE
            IF Solver.seen[var] THEN CONTINUE
            IF Solver.decision_level[var] == 0 THEN CONTINUE

            Solver.seen[var] = TRUE
            Solver.bump_variables.push(var)

            IF Solver.decision_level[var] == Solver.current_level THEN:
                path_count++  // Count paths from current decision level
            ELSE:
                learned_clause.push(lit)  // Literal from earlier decision level
            END IF
        END FOR

        // Find next literal on trail from current level marked as seen
        WHILE NOT Solver.seen[ABS(Solver.trail[trail_index])] DO:
            trail_index--
        END WHILE

        resolved_lit = Solver.trail[trail_index--]
        pivot_variable = ABS(resolved_lit)
        Solver.seen[pivot_variable] = FALSE
        path_count--

        // Stop when exactly one literal from current decision level remains (1st UIP)
        IF path_count == 0 THEN:
            asserting_literal = -resolved_lit
            BREAK
        END IF

        clause_to_process = Solver.reason[pivot_variable]
    END WHILE

    learned_clause[0] = asserting_literal

    // Calculate assertion backtrack level (max level among remaining literals)
    backtrack_level = 0
    max_level_idx = 1

    FOR i = 1 TO learned_clause.size - 1 DO:
        lvl = Solver.decision_level[ABS(learned_clause[i])]
        IF lvl > backtrack_level THEN:
            backtrack_level = lvl
            max_level_idx = i
        END IF
    END FOR

    // Swap highest level literal into index 1 so index 0 and 1 are watched
    IF learned_clause.size > 1 THEN:
        SWAP(learned_clause[1], learned_clause[max_level_idx])
    END IF

    // Clear seen scratchpad
    FOR each var IN Solver.bump_variables DO:
        Solver.seen[var] = FALSE
    END FOR

    RETURN (learned_clause, backtrack_level)
END FUNCTION
```

#### `Solve(Solver, num_vars, original_clauses, num_clauses) -> Boolean`
```
FUNCTION Solve(Solver, num_vars, original_clauses, num_clauses):
    InitializeTrail(Solver, num_vars)
    VSIDS_Initialize(Solver.vsids, Solver.memory_manager, num_vars, 0.95)

    // Load initial clauses into watch lists or enqueue unit clauses
    FOR i = 0 TO num_clauses - 1 DO:
        clause = original_clauses[i]
        Solver.clauses.push(clause)

        IF clause.size == 1 THEN:
            IF NOT Enqueue(Solver, clause.lits[0], clause) THEN:
                RETURN FALSE  // Root contradiction -> UNSAT
            END IF
        ELSE:
            AttachClauseWatches(Solver, clause)
        END IF
    END FOR

    // Main CDCL Search Loop
    WHILE TRUE DO:
        conflict = Propagate(Solver)

        IF conflict IS NOT NULL THEN:
            // Conflict at root level 0 means formula is UNSAT
            IF Solver.current_level == 0 THEN:
                RETURN FALSE
            END IF

            // Analyze conflict and derive 1-UIP learned clause
            (learned_clause, backtrack_level) = Analyze(Solver, conflict)

            // Bump VSIDS scores for all variables in conflict graph
            FOR each var IN Solver.bump_variables DO:
                VSIDS_Bump(Solver.vsids, var)
            END FOR
            VSIDS_Decay(Solver.vsids)

            // Register and watch new learned clause
            learned = CreateClause(learned_clause, is_learned: TRUE)
            Solver.clauses.push(learned)
            AttachClauseWatches(Solver, learned)

            // Non-chronological backjump
            Backtrack(Solver, backtrack_level)

            // Enqueue asserting literal at the backjump level
            asserting_lit = learned_clause[0]
            IF NOT Enqueue(Solver, asserting_lit, learned) THEN:
                RETURN FALSE
            END IF

            CONTINUE
        END IF

        // Pick next branching decision via VSIDS
        next_var = VSIDS_SelectVariable(Solver.vsids, Solver.assignment)
        IF next_var == NO_VARIABLE THEN:
            RETURN TRUE  // All variables assigned with no conflicts -> SAT
        END IF

        polarity = SelectPolarity(next_var)  // e.g. VAL_FALSE
        decision_lit = (polarity == VAL_TRUE) ? next_var : -next_var

        // Advance decision level and enqueue guess
        NewDecisionLevel(Solver)
        Enqueue(Solver, decision_lit, NO_REASON)
    END WHILE
END FUNCTION
```

---

## 4. End-to-End Workflow Execution Summary

```
                +----------------------------+
                |    Input CNF Clauses       |
                +--------------+-------------+
                               |
                               v
                +----------------------------+
                | Initialize Trail & Memory  |
                | Init VSIDS with MemManager |
                +--------------+-------------+
                               |
                               v
            +------------------------------------+
            | Level 0 Unit Propagation (BCP)     |
            +------------------+-----------------+
                               |
               +---------------+---------------+
               |                               |
         [Root Conflict]                 [No Conflict]
               |                               |
               v                               v
          +---------+              +-----------------------+
          |  UNSAT  |              | VSIDS Select Variable |
          +---------+              +-----------+-----------+
                                               |
                                     +---------+---------+
                                     |                   |
                               [Unassigned Var]    [All Assigned]
                                     |                   |
                                     v                   v
                        +----------------------+    +---------+
                        |  New Decision Level  |    |   SAT   |
                        |    Enqueue Guess     |    +---------+
                        +----------+-----------+
                                   |
                                   v
                        +----------------------+
                        | 2WL Unit Propagation |
                        +----------+-----------+
                                   |
                     +-------------+-------------+
                     |                           |
                 [Conflict]                [No Conflict]
                     |                           |
                     v                           |
        +-------------------------+              |
        | 1-UIP Conflict Analysis |              |
        | VSIDS Bump & Decay      |              |
        | Add Learned Clause      |              |
        | Non-Chronological Jump  |              |
        | Enqueue Asserting Lit   |              |
        +------------+------------+              |
                     |                           |
                     +---------------------------+
```
