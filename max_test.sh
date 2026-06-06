#!/bin/bash
#
# MAX TEST - One file, all flags, max detection power
#

if [ -z "$1" ]; then
    echo "Usage: $0 <source.c>"
    echo ""
    echo "Tests ONE file with ALL c_tester flags:"
    echo "  - Default (ASan + UBSan)"
    echo "  - --analyzer (GCC static)"
    echo "  - --valgrind (deep memory)"
    echo "  - --tsan (ThreadSanitizer)"
    echo "  - --clang-tidy (clang-tidy static analysis)"
    echo ""
    echo "Examples:"
    echo "  $0 my_buggy.c"
    exit 1
fi

FILE=$1
C_TESTER="./c_tester"

if [ ! -f "$FILE" ]; then
    echo "Error: File not found: $FILE"
    exit 1
fi

echo "========================================"
echo "  c_tester - MAX POWER TEST"
echo "========================================"
echo ""
echo "Testing: $FILE"
echo ""

# Run a single test mode and print results
# Returns 0 if errors found, 1 if clean
run_test() {
    local label="$1"
    shift
    local OUTPUT=$($C_TESTER $FILE "$@" --no-color 2>&1)

    echo "========================================"
    echo "$label"
    echo "========================================"

    if echo "$OUTPUT" | grep -qa "\[COMPILE ERROR\]"; then
        echo "[COMPILE ERROR]"
        echo "$OUTPUT" | sed -n '/\[COMPILE ERROR\]/{n;p;n;p;}' | head -3
        return 0
    elif echo "$OUTPUT" | grep -qa "\[ERROR\]"; then
        echo "$OUTPUT" | grep -aE "^\[ERROR\]|\[OK\]" | head -5
        echo "$OUTPUT" | grep -aE "Fix:" | head -3
        return 0
    else
        echo "$OUTPUT" | grep -aE "^\[OK\]|\[COMPILE"
        return 1
    fi
}

DETECTION=""

if run_test "[1] DEFAULT (ASan + UBSan)"; then
    DETECTION="$DETECTION DEFAULT"
fi
echo ""

if run_test "[2] --ANALYZER (GCC static)" --analyzer; then
    DETECTION="$DETECTION --analyzer"
fi
echo ""

if run_test "[3] --VALGRIND (deep mem)" --valgrind; then
    DETECTION="$DETECTION --valgrind"
fi
echo ""

if run_test "[4] --TSAN (ThreadSanitizer)" --tsan; then
    DETECTION="$DETECTION --tsan"
fi
echo ""

if run_test "[5] --CLANG-TIDY (static analysis)" --clang-tidy; then
    DETECTION="$DETECTION --clang-tidy"
fi
echo ""

if run_test "[6] MAX MODE (all modes)" --max; then
    DETECTION="$DETECTION --max"
fi
echo ""

echo "========================================"
echo "  SUMMARY"
echo "========================================"

if [ -n "$DETECTION" ]; then
    echo "DETECTED by:$DETECTION"
else
    echo "No errors detected (all clean)"
fi

echo ""
echo "Done!"
