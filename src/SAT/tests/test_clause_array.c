/**
 * test_clause_array.c  ── Unit Test Suite for ClauseArray Arena
 * ==============================================================
 * Validates contiguous arena memory allocation, 2-WL watch lists,
 * literal swapping, tombstoning, and in-place sliding compaction.
 */

#include "test_harness.h"
#include "clause_array.h"

static void test_init_and_destroy(void) {
    ClauseArray ca;
    bool ok = clause_array_init(&ca, 128);
    TEST_ASSERT(ok, "clause_array_init failed");
    TEST_ASSERT(ca.memory != NULL, "memory buffer should not be NULL");
    TEST_ASSERT(ca.capacity >= 128, "capacity below requested");
    TEST_ASSERT(ca.used_words == 1, "offset 0 should be reserved for CLAUSE_REF_NULL");
    TEST_ASSERT(ca.clause_count == 0, "initial clause count should be 0");

    clause_array_destroy(&ca);
    TEST_ASSERT(ca.memory == NULL, "memory should be NULL after destroy");
    TEST_ASSERT(ca.capacity == 0, "capacity should be 0 after destroy");
}

static void test_allocate_and_access(void) {
    ClauseArray ca;
    clause_array_init(&ca, 64);

    int32_t lits1[] = {10, -20, 30};
    ClauseRef ref1 = clause_array_allocate(&ca, lits1, 3, false, 0.0f);
    TEST_ASSERT(ref1 != CLAUSE_REF_NULL, "ref1 allocation failed");
    TEST_ASSERT(clause_array_get_size(&ca, ref1) == 3, "expected size 3 for ref1");
    TEST_ASSERT(!clause_array_is_learned(&ca, ref1), "ref1 should not be learned");
    TEST_ASSERT(!clause_array_is_deleted(&ca, ref1), "ref1 should not be deleted");

    const int32_t* stored1 = clause_array_get_literals_const(&ca, ref1);
    TEST_ASSERT(stored1[0] == 10 && stored1[1] == -20 && stored1[2] == 30, "ref1 literals mismatch");

    int32_t lits2[] = {-5, 15};
    ClauseRef ref2 = clause_array_allocate(&ca, lits2, 2, true, 2.5f);
    TEST_ASSERT(ref2 != CLAUSE_REF_NULL, "ref2 allocation failed");
    TEST_ASSERT(clause_array_get_size(&ca, ref2) == 2, "expected size 2 for ref2");
    TEST_ASSERT(clause_array_is_learned(&ca, ref2), "ref2 should be learned");
    TEST_ASSERT(clause_array_get_activity(&ca, ref2) == 2.5f, "ref2 activity mismatch");

    clause_array_destroy(&ca);
}

static void test_swap_and_modify_literals(void) {
    ClauseArray ca;
    clause_array_init(&ca, 64);

    int32_t lits[] = {1, 2, 3, 4};
    ClauseRef ref = clause_array_allocate(&ca, lits, 4, false, 0.0f);

    clause_array_swap_literals(&ca, ref, 0, 2);
    TEST_ASSERT(clause_array_get_literal(&ca, ref, 0) == 3, "expected literal 3 at pos 0 after swap");
    TEST_ASSERT(clause_array_get_literal(&ca, ref, 2) == 1, "expected literal 1 at pos 2 after swap");

    clause_array_set_literal(&ca, ref, 1, -99);
    TEST_ASSERT(clause_array_get_literal(&ca, ref, 1) == -99, "expected literal -99 at pos 1");

    clause_array_destroy(&ca);
}

static void test_watchlist_operations(void) {
    WatchList wl;
    bool ok = watch_list_init(&wl, 2);
    TEST_ASSERT(ok, "watch_list_init failed");
    TEST_ASSERT(wl.size == 0, "initial size should be 0");

    watch_list_push(&wl, 101, 2);
    watch_list_push(&wl, 202, -3);
    watch_list_push(&wl, 303, 4); // triggers resize

    TEST_ASSERT(wl.size == 3, "watch list size should be 3");
    TEST_ASSERT(wl.watchers[0].clause_ref == 101 && wl.watchers[0].blocking_literal == 2, "w0 mismatch");
    TEST_ASSERT(wl.watchers[1].clause_ref == 202 && wl.watchers[1].blocking_literal == -3, "w1 mismatch");
    TEST_ASSERT(wl.watchers[2].clause_ref == 303 && wl.watchers[2].blocking_literal == 4, "w2 mismatch");

    watch_list_clear(&wl);
    TEST_ASSERT(wl.size == 0, "size should be 0 after clear");

    watch_list_destroy(&wl);
    TEST_ASSERT(wl.watchers == NULL, "watchers should be NULL after destroy");
}

typedef struct {
    ClauseRef old_ref;
    ClauseRef new_ref;
} RemapEntry;

static void remap_callback(ClauseRef old_ref, ClauseRef new_ref, void* ctx) {
    RemapEntry* entry = (RemapEntry*)ctx;
    if (old_ref == entry->old_ref) {
        entry->new_ref = new_ref;
    }
}

static void test_compaction(void) {
    ClauseArray ca;
    clause_array_init(&ca, 64);

    int32_t l1[] = {1, 2};
    int32_t l2[] = {3, 4, 5};
    int32_t l3[] = {6, 7};

    ClauseRef r1 = clause_array_allocate(&ca, l1, 2, false, 0.0f);
    (void)r1;
    ClauseRef r2 = clause_array_allocate(&ca, l2, 3, true, 1.0f);
    ClauseRef r3 = clause_array_allocate(&ca, l3, 2, true, 2.0f);

    TEST_ASSERT(ca.clause_count == 3, "should have 3 clauses");

    // Tombstone middle clause r2
    clause_array_mark_deleted(&ca, r2);
    TEST_ASSERT(clause_array_is_deleted(&ca, r2), "r2 should be deleted");
    TEST_ASSERT(clause_array_compaction_needed(&ca, 0.20), "compaction should be needed");

    RemapEntry remap = { .old_ref = r3, .new_ref = CLAUSE_REF_NULL };
    clause_array_compact(&ca, remap_callback, &remap);

    TEST_ASSERT(ca.clause_count == 2, "should have 2 active clauses remaining");
    TEST_ASSERT(remap.new_ref != CLAUSE_REF_NULL && remap.new_ref < r3, "r3 should slide to a lower offset");
    TEST_ASSERT(clause_array_get_size(&ca, remap.new_ref) == 2, "compacted r3 size mismatch");
    TEST_ASSERT(clause_array_get_literal(&ca, remap.new_ref, 0) == 6, "compacted r3 lit 0 mismatch");
    TEST_ASSERT(clause_array_get_literal(&ca, remap.new_ref, 1) == 7, "compacted r3 lit 1 mismatch");

    clause_array_destroy(&ca);
}

int main(void) {
    printf("=== ClauseArray Arena Test Suite ===\n");
    RUN_TEST(test_init_and_destroy);
    RUN_TEST(test_allocate_and_access);
    RUN_TEST(test_swap_and_modify_literals);
    RUN_TEST(test_watchlist_operations);
    RUN_TEST(test_compaction);
    REPORT_AND_EXIT();
}
