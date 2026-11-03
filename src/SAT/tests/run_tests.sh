#!/usr/bin/env bash
set -e

# Change to the script's directory (src/SAT/tests)
cd "$(dirname "$0")"

echo "=================================================="
echo " Building CDCL Test Suites..."
echo "=================================================="
make all

echo ""
echo "=================================================="
echo " Running All CDCL Tests Sequentially (T0 -> T7)..."
echo "=================================================="

TESTS=(
    "test_memory_allocation"
    "test_enqueue_trail"
    "test_propagate"
    "test_analyze_conflict"
    "test_backtrack"
    "test_restart"
    "test_database_refresh"
    "test_solve_integration"
    "test_benchmarks"
    "test_10m_clauses"
)

failed=0
passed=0

for test in "${TESTS[@]}"; do
    echo ""
    echo ">>> Running $test..."
    if "./$test"; then
        passed=$((passed + 1))
    else
        echo "[ERROR] $test failed!"
        failed=$((failed + 1))
    fi
done

echo ""
echo "=================================================="
echo " Final Summary: $passed passed, $failed failed"
echo "=================================================="

if [ "$failed" -eq 0 ]; then
    echo " All test suites passed successfully!"
    exit 0
else
    echo " Some test suites failed."
    exit 1
fi
