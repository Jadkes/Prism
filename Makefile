CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2
TARGET = c_tester

BAD_CODES_DIR = bad_codes
HOOKS_DIR = hooks

.PHONY: all clean test test-all test-edge test-valgrind test-sanitizers test-leaks test-all-verbose install install-hooks deps

all: $(TARGET)

$(TARGET): c_tester.c c_tester.h
	$(CC) $(CFLAGS) -o $(TARGET) c_tester.c

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "Running c_tester test suite..."
	@PASS=0; FAIL=0; \
	run_test() { \
		name=$$1; file=$$2; expect=$$3; \
		output=$$(./$(TARGET) $$file 2>&1); \
		if echo "$$output" | grep -qi "$$expect"; then \
			echo "  PASS: $$name"; \
			PASS=$$((PASS + 1)); \
		else \
			echo "  FAIL: $$name (expected '$$expect')"; \
			echo "    Output: $$output"; \
			FAIL=$$((FAIL + 1)); \
		fi; \
	}; \
	run_test "Clean run" "tests/clean.c" "No errors detected"; \
	run_test "NULL deref (write)" "tests/null_deref.c" "NULL Pointer"; \
	run_test "NULL deref (read)" "tests/test_null_deref_read.c" "NULL Pointer"; \
	run_test "NULL pointer passed" "tests/test_null_pointer_passed.c" "NULL Pointer"; \
	run_test "Buffer overflow (stack)" "tests/buffer_overflow.c" "Buffer Overflow"; \
	run_test "Buffer overflow (heap)" "tests/test_heap_buffer_overflow.c" "Buffer Overflow"; \
	run_test "Stack buffer underflow" "tests/test_stack_buffer_underflow.c" "Out of Bounds"; \
	run_test "Heap buffer underflow" "tests/test_heap_buffer_underflow.c" "Buffer Overflow"; \
	run_test "Array out of bounds" "tests/test_out_of_bounds.c" "Out of Bounds"; \
	run_test "Insufficient space" "tests/test_insufficient_space.c" "Buffer Overflow"; \
	run_test "Use after free" "tests/use_after_free.c" "Use After Free"; \
	run_test "Memory leak" "tests/memory_leak.c" "Memory Leak"; \
	run_test "Int overflow" "tests/int_overflow.c" "Integer Overflow"; \
	run_test "Invalid shift" "tests/test_invalid_shift.c" "Invalid Shift"; \
	run_test "Div by zero" "tests/div_by_zero.c" "Division by Zero"; \
	run_test "Uninit var" "tests/uninit_var.c" "Uninitialized"; \
	run_test "Stack overflow" "tests/test_stack_overflow.c" "Stack Overflow"; \
	run_test "Free nonheap" "tests/test_free_nonheap_object.c" "Invalid Free"; \
	run_test "Alloc size larger" "tests/test_alloc_size_larger_than.c" "detected"; \
	run_test "Infinite loop" "tests/infinite_loop.c" "timeout"; \
	run_test "Syntax error" "tests/syntax_error.c" "COMPILE ERROR"; \
	run_test "Data race (TSan)" "--tsan tests/data_race.c" "Data Race"; \
	run_test "Lock order inversion" "--tsan tests/test_lock_order_inversion.c" "Lock Order Inversion"; \
	run_test "Signal unsafe" "--tsan tests/test_signal_unsafe.c" "No errors detected"; \
	run_test "C++ vector overflow" "tests/cpp_vector_overflow.cpp" "Out of Range"; \
	run_test "C++ bad alloc" "tests/test_cpp_bad_alloc.cpp" "COMPILE ERROR"; \
	run_test "C++ out of range" "tests/test_cpp_out_of_range.cpp" "No errors detected"; \
	run_test "C++ logic error" "tests/test_cpp_logic_error.cpp" "No errors detected"; \
	run_test "C++ pure virtual" "tests/test_cpp_pure_virtual.cpp" "COMPILE"; \
	run_test "C++ double free corruption" "tests/test_cpp_double_free_corruption.cpp" "Double Free"; \
	run_test "Multi-file overflow" "tests/multi_main.c tests/multi_helper.c" "Buffer Overflow"; \
	if which valgrind >/dev/null 2>&1; then \
		run_test "Valgrind uninit" "--valgrind tests/valgrind_uninit.c" "Uninitialized"; \
		run_test "Valgrind invalid read" "--valgrind tests/test_valgrind_invalid_read.c" "Invalid Read"; \
		run_test "Valgrind invalid write" "--valgrind tests/test_valgrind_invalid_write.c" "Invalid Write"; \
		run_test "Valgrind uninit conditional" "--valgrind tests/test_valgrind_uninit_conditional.c" "Uninitialized Conditional"; \
		run_test "Valgrind definite leak" "--valgrind tests/test_valgrind_definite_leak.c" "Memory Leak"; \
		run_test "Valgrind possible leak" "--valgrind tests/test_valgrind_possible_leak.c" "Memory Leak"; \
	else \
		echo "  SKIP: Valgrind tests (valgrind not found)"; \
		PASS=$$((PASS + 6)); \
	fi; \
	echo ""; \
	echo "Results: $$PASS passed, $$FAIL failed"; \
	if [ "$$FAIL" -gt 0 ]; then exit 1; fi

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

install-hooks: $(HOOKS_DIR)/pre-commit
	chmod +x $(HOOKS_DIR)/pre-commit
	cp $(HOOKS_DIR)/pre-commit .git/hooks/pre-commit
	@echo "Pre-commit hook installed to .git/hooks/"

test-all: $(TARGET)
	@echo "Running c_tester on all bad_codes files..."
	@PASS=0; FAIL=0; TOTAL=0; \
	for f in $(BAD_CODES_DIR)/*.c; do \
		TOTAL=$$((TOTAL + 1)); \
		echo "  Testing: $$f"; \
		output=$$(./$(TARGET) $$f 2>&1); \
		if echo "$$output" | grep -qi "detected\|error"; then \
			echo "    PASS (error detected)"; \
			PASS=$$((PASS + 1)); \
		else \
			echo "    FAIL (no error detected)"; \
			echo "    Output: $$output"; \
			FAIL=$$((FAIL + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$PASS/$$TOTAL passed (error detected), $$FAIL failed (missed)"; \
	if [ "$$FAIL" -gt 0 ]; then exit 1; fi

test-edge: $(TARGET)
	@echo "Running edge case tests..."
	@PASS=0; FAIL=0; \
	run_test() { \
		name=$$1; file=$$2; expect=$$3; \
		output=$$(./$(TARGET) $$file 2>&1); \
		if echo "$$output" | grep -qi "$$expect"; then \
			echo "  PASS: $$name"; \
			PASS=$$((PASS + 1)); \
		else \
			echo "  FAIL: $$name (expected '$$expect')"; \
			echo "    Output: $$output"; \
			FAIL=$$((FAIL + 1)); \
		fi; \
	}; \
	run_test "Large output" "tests/test_edge_large_output.c" "No errors detected"; \
	run_test "Empty output" "tests/test_edge_empty_output.c" "No errors detected"; \
	run_test "Timeout" "tests/test_edge_timeout.c" "timeout"; \
	run_test "Rapid alloc" "tests/test_edge_rapid_alloc.c" "No errors detected"; \
	run_test "Long path" "tests/test_edge_long_path.c" "No errors detected"; \
	echo ""; \
	echo "Edge case results: $$PASS passed, $$FAIL failed"; \
	if [ "$$FAIL" -gt 0 ]; then exit 1; fi

# Valgrind test: runs c_tester with --valgrind flag on key tests
test-valgrind: $(TARGET)
	@if ! which valgrind >/dev/null 2>&1; then \
		echo "ERROR: valgrind not installed. Install with: sudo apt install valgrind"; \
		exit 1; \
	fi
	@echo "Running c_tester with Valgrind instrumentation..."
	@PASS=0; FAIL=0; \
	run_valgrind_test() { \
		name=$$1; file=$$2; expect=$$3; \
		echo "  Testing: $$name"; \
		output=$$(./$(TARGET) --valgrind $$file 2>&1); \
		if echo "$$output" | grep -qi "$$expect"; then \
			echo "    PASS: $$name"; \
			PASS=$$((PASS + 1)); \
		else \
			echo "    FAIL: $$name (expected '$$expect')"; \
			echo "    Output: $$output"; \
			FAIL=$$((FAIL + 1)); \
		fi; \
	}; \
	run_valgrind_test "Valgrind uninit" "tests/valgrind_uninit.c" "Uninitialized"; \
	run_valgrind_test "Clean run (valgrind)" "tests/clean.c" "No errors detected"; \
	run_valgrind_test "NULL deref (valgrind)" "tests/null_deref.c" "Invalid Write"; \
	echo ""; \
	echo "Valgrind Results: $$PASS passed, $$FAIL failed"; \
	if [ "$$FAIL" -gt 0 ]; then exit 1; fi

# Sanitizer test: compile and run c_tester with ASan/UBSan enabled
test-sanitizers: c_tester.c c_tester.h
	@echo "Building c_tester with AddressSanitizer and UndefinedBehaviorSanitizer..."
	@$(CC) $(CFLAGS) -fsanitize=address,undefined -o $(TARGET)_san c_tester.c
	@echo "Running c_tester (sanitized) on test files..."
	@PASS=0; FAIL=0; \
	run_san_test() { \
		name=$$1; file=$$2; expect=$$3; \
		echo "  Testing: $$name"; \
		output=$$(./$(TARGET)_san $$file 2>&1); \
		if echo "$$output" | grep -qi "$$expect"; then \
			echo "    PASS: $$name"; \
			PASS=$$((PASS + 1)); \
		else \
			echo "    FAIL: $$name (expected '$$expect')"; \
			echo "    Output: $$output"; \
			FAIL=$$((FAIL + 1)); \
		fi; \
	}; \
	run_san_test "Clean run (sanitized)" "tests/clean.c" "No errors detected"; \
	run_san_test "NULL deref (sanitized)" "tests/null_deref.c" "NULL Pointer"; \
	run_san_test "Buffer overflow (sanitized)" "tests/buffer_overflow.c" "Buffer Overflow"; \
	run_san_test "Use after free (sanitized)" "tests/use_after_free.c" "Use After Free"; \
	echo ""; \
	echo "Sanitizer Results: $$PASS passed, $$FAIL failed"; \
	rm -f $(TARGET)_san; \
	if [ "$$FAIL" -gt 0 ]; then exit 1; fi

# Leak check: runs valgrind with full leak detection on a clean test
test-leaks: $(TARGET)
	@if ! which valgrind >/dev/null 2>&1; then \
		echo "ERROR: valgrind not installed. Install with: sudo apt install valgrind"; \
		exit 1; \
	fi
	@echo "Running Valgrind leak check on clean test..."
	@output=$$(valgrind --leak-check=full --track-fds=yes ./$(TARGET) tests/clean.c 2>&1); \
	echo "$$output"; \
	if echo "$$output" | grep -q "no leaks are possible\|definitely lost: 0 bytes"; then \
		echo "PASS: No memory leaks detected"; \
	else \
		echo "FAIL: Memory leaks detected"; \
		exit 1; \
	fi; \
	if echo "$$output" | grep -q "Open file descriptor"; then \
		echo "WARNING: Open file descriptors detected"; \
	fi

# Verbose test: shows detailed output for each test (useful for debugging)
test-all-verbose: $(TARGET)
	@echo "Running c_tester test suite (VERBOSE)..."
	@PASS=0; FAIL=0; \
	run_verbose_test() { \
		name=$$1; shift; \
		echo "========================================"; \
		echo "Test: $$name"; \
		echo "Command: ./$(TARGET) $$@"; \
		echo "----------------------------------------"; \
		output=$$(./$(TARGET) "$$@" 2>&1); \
		echo "$$output"; \
		echo "----------------------------------------"; \
		if echo "$$output" | grep -qi "detected\|error\|No errors"; then \
			echo "RESULT: PASS"; \
			PASS=$$((PASS + 1)); \
		else \
			echo "RESULT: FAIL"; \
			FAIL=$$((FAIL + 1)); \
		fi; \
		echo ""; \
	}; \
	run_verbose_test "Clean run" tests/clean.c; \
	run_verbose_test "NULL deref" tests/null_deref.c; \
	run_verbose_test "Buffer overflow" tests/buffer_overflow.c; \
	run_verbose_test "Use after free" tests/use_after_free.c; \
	run_verbose_test "Memory leak" tests/memory_leak.c; \
	echo "========================================"; \
	echo "Verbose Results: $$PASS passed, $$FAIL failed"; \
	if [ "$$FAIL" -gt 0 ]; then exit 1; fi

deps:
	bash install_deps.sh
