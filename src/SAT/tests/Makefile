CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -g -fsanitize=address,undefined
INCLUDES = -I../CDCL -I../CDCL/Clauses
CDCL_SRC = ../CDCL/CDCL.c ../CDCL/VSIDS.c ../CDCL/memory.c ../CDCL/Clauses/clause_array.c
LDFLAGS  = -lm

TESTS = test_clause_array \
        test_memory_allocation \
        test_enqueue_trail \
        test_propagate \
        test_analyze_conflict \
        test_backtrack \
        test_restart \
        test_database_refresh \
        test_solve_integration \
        test_benchmarks \
        test_10m_clauses

.PHONY: all run clean

all: $(TESTS)

%: %.c $(CDCL_SRC) test_harness.h
	$(CC) $(CFLAGS) $(INCLUDES) $< $(CDCL_SRC) $(LDFLAGS) -o $@

run: all
	@failed=0; \
	for t in $(TESTS); do \
		echo "========================================"; \
		./$$t || failed=$$((failed + 1)); \
	done; \
	echo "========================================"; \
	if [ $$failed -eq 0 ]; then \
		echo "ALL SUITES PASSED!"; \
		exit 0; \
	else \
		echo "$$failed SUITE(S) FAILED!"; \
		exit 1; \
	fi

clean:
	rm -rf $(TESTS) *.dSYM
