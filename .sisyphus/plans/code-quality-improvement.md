# c_tester Code Quality Improvement Plan

## TL;DR

> **Quick Summary**: Fix 6 code quality issues (resource leaks, race condition, code duplication, signal safety) and upgrade test suite to glibc/open-source standards with comprehensive sanitizer-verified test cases.
>
> **Deliverables**:
> - Fixed resource leaks in `run_binary()` and `run_with_valgrind()`
> - Fixed `generate_temp_path()` race condition (mkstemp + unlink)
> - Refactored shared execution logic into `run_with_timeout()` helper
> - Minimal signal safety improvements
> - Upgraded test suite with comprehensive test cases (glibc-style)
> - Enhanced Makefile test targets with Valgrind and sanitizer verification
>
> **Estimated Effort**: Medium
> **Parallel Execution**: YES - 3 waves
> **Critical Path**: Fix bugs → Refactor → Upgrade tests

---

## Context

### Original Request
Improve code quality of c_tester C error detection tool and commit changes. User explicitly requested test suite upgrade to glibc/open-source standards.

### Interview Summary
**Key Discussions**:
- C Standard: C99 or later (keep mixed declarations, they're valid)
- Unit tests for tool's own functions: Note for future, NOT this work
- Fix order: Fix bugs first (safer), then refactor
- Signal safety: Minimal (add volatile if needed, no major changes)
- Test suite: Upgrade to comprehensive glibc-style coverage

**User Answers**:
- C Standard: "C99 or later (Recommended)"
- Unit tests: "Note for future (Recommended)"
- Fix order: "Fix bugs first, then refactor (Recommended)"
- Signal safety: "Minimal (Recommended)"
- Test suite upgrade: "Upgrade like glibc or any open-source tool"

### Metis Review
**Identified Gaps** (addressed):
- Added validation steps for assumptions (read code before fixing)
- Added executable acceptance criteria (exact commands with expected output)
- Added explicit scope exclusions (no unit tests for tool itself)
- Added edge case coverage (timing, fork failure, large output)
- Added behavior preservation verification via `diff` of baseline

---

## Work Objectives

### Core Objective
Improve c_tester code quality by fixing bugs, reducing duplication, and upgrading test suite to production standards.

### Concrete Deliverables
- `c_tester.c`: Fixed resource leaks, race condition, refactored shared code
- `c_tester.c`: Minimal signal safety improvements
- `tests/`: Expanded test suite with comprehensive edge cases
- `bad_codes_v2/`: Additional test cases for all error patterns
- `Makefile`: Enhanced test targets with Valgrind and sanitizer verification
- Test documentation: glibc-style test organization

### Definition of Done
- [ ] All resource leaks fixed (verified with Valgrind --track-fds=yes)
- [ ] Race condition in `generate_temp_path()` fixed
- [ ] `run_binary()` and `run_with_valgrind()` refactored (code duplication eliminated)
- [ ] Minimal signal safety improvements applied
- [ ] Test suite expanded to cover all error patterns in `error_patterns[]`
- [ ] Makefile `test` target runs with Valgrind and sanitizer verification
- [ ] All tests pass with `gcc -Wall -Wextra -Werror -fsanitize=address,undefined`
- [ ] Behavior preserved (diff of baseline vs after shows no changes)

### Must Have
- Fix resource leaks on error paths in `run_binary()` and `run_with_valgrind()`
- Fix `generate_temp_path()` race condition (mkstemp + unlink)
- Refactor shared execution logic into `run_with_timeout()` helper
- Upgrade test suite with comprehensive test cases
- Verify no memory leaks with Valgrind
- Verify no undefined behavior with UBSan

### Must NOT Have (Guardrails)
- NO unit tests for tool's own functions (note for future)
- NO changing public API in `c_tester.h`
- NO new error detection features
- NO "fixing" C99 mixed declarations (they're valid)
- NO major signal architecture changes
- NO refactoring beyond `run_binary()` and `run_with_valgrind()`

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** - ALL verification is agent-executed. No exceptions.

### Test Decision
- **Infrastructure exists**: YES - Makefile with `test` and `test-all` targets
- **Automated tests**: Upgrade existing + add new comprehensive tests
- **Framework**: GCC/G++ with sanitizers, Valgrind for leak detection

### QA Policy
Every task MUST include agent-executed QA scenarios.

- **C code**: Compile with `gcc -Wall -Wextra -Werror -fsanitize=address,undefined`
- **Test execution**: Run through c_tester, verify error detection
- **Memory leaks**: Run with `valgrind --track-fds=yes --leak-check=full`
- **Behavior preservation**: `diff baseline.txt after_fix.txt` (must be empty)

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (Start Immediately - bug fixes):
├── Task 1: Fix resource leaks in run_binary() [quick]
├── Task 2: Fix resource leaks in run_with_valgrind() [quick]
├── Task 3: Fix generate_temp_path() race condition [quick]
└── Task 4: Minimal signal safety improvements [quick]

Wave 2 (After Wave 1 - refactoring):
├── Task 5: Extract run_with_timeout() helper [deep]
├── Task 6: Refactor run_binary() to use helper [deep]
└── Task 7: Refactor run_with_valgrind() to use helper [deep]

Wave 3 (After Wave 2 - test suite upgrade):
├── Task 8: Add test cases for all error patterns [unspecified-high]
├── Task 9: Add edge case tests (large output, timeout, etc.) [unspecified-high]
├── Task 10: Add bad_codes_v2 tests for missed patterns [unspecified-high]
├── Task 11: Enhance Makefile test targets [quick]
└── Task 12: Create test documentation (glibc-style) [writing]

Final Verification (After ALL tasks):
├── Task F1: Plan compliance audit (oracle)
├── Task F2: Code quality review (unspecified-high)
├── Task F3: Real manual QA - run all tests (unspecified-high)
└── Task F4: Scope fidelity check (deep)
```

### Dependency Matrix

| Task | Depends On | Blocks | Parallel Group |
|------|-----------|--------|----------------|
| 1,2,3,4 | - | 5,6,7 | Wave 1 |
| 5,6,7 | 1,2,3,4 | 8-12 | Wave 2 |
| 8-12 | 5,6,7 | F1-F4 | Wave 3 |
| F1-F4 | 8-12 | - | Final |

### Agent Dispatch Summary

- **Wave 1**: 4 tasks → `quick` (simple bug fixes)
- **Wave 2**: 3 tasks → `deep` (refactoring requires thorough understanding)
- **Wave 3**: 5 tasks → `unspecified-high` (test creation) + `writing` (docs)
- **Final**: 4 tasks → `oracle`, `unspecified-high`, `deep`

---

## TODOs

> Implementation + Test = ONE Task. Never separate.
> EVERY task MUST have: Recommended Agent Profile + Parallelization info + QA Scenarios.

- [x] 1. Fix resource leaks in run_binary() error paths

  **What to do**:
  - Read c_tester.c lines 389-526 (run_binary function)
  - Locate error paths where fork() fails (lines 408-411) and pipes were opened but not closed
  - On fork() failure: close(stdout_pipe[0]), close(stderr_pipe[0]) before returning -1
  - Also check: if pipe() fails (lines 404-405), ensure no pipe FDs need closing (they don't since pipe() failed)
  - Verify all error paths in the select() loop also close pipe FDs properly
  - Rebuild with `gcc -Wall -Wextra -Werror -fsanitize=address,undefined -o c_tester c_tester.c`

  **Must NOT do**:
  - Do NOT change the fork/exec logic itself
  - Do NOT add new features or change behavior

  **Recommended Agent Profile**:
  > Category: `quick`
  > Reason: Simple bug fix - close FDs on error paths. Straightforward, no deep analysis needed.
  > Skills: None needed - basic C fix.

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 2, 3, 4)
  - **Parallel Group**: Wave 1
  - **Blocks**: Task 5, 6, 7 (refactoring depends on bugs being fixed first)
  - **Blocked By**: None

  **References**:
  - `c_tester.c:389-526` - run_binary() function (full implementation)
  - `c_tester.c:404-411` - Pipe creation and fork() failure paths (THE BUG)
  - `c_tester.c:431-436` - Parent process closing pipes (CORRECT pattern to follow)
  - `AGENTS.md` "Resource cleanup" section - Linux kernel style cleanup patterns

  **Acceptance Criteria**:
  - [ ] c_tester.c compiles with `-Wall -Wextra -Werror`
  - [ ] Resource leaks fixed: all error paths close pipe FDs
  - [ ] Run `valgrind --track-fds=yes ./c_tester tests/clean.c` → No "Open file descriptor" warnings for pipes

  **QA Scenarios**:

  ```
  Scenario: Verify resource leaks fixed (happy path - no leak)
    Tool: Bash (valgrind)
    Preconditions: c_tester compiled with -fsanitize=address,undefined
    Steps:
      1. valgrind --track-fds=yes --leak-check=full ./c_tester tests/clean.c 2>&1 | tee /tmp/valgrind_test1.txt
      2. grep -q "Open file descriptor" /tmp/valgrind_test1.txt
    Expected Result: grep exits with code 1 (no matches found = no leaks)
    Failure Indicators: "Open file descriptor N, ... pipe" in valgrind output
    Evidence: .sisyphus/evidence/task-1-valgrind-clean.txt

  Scenario: Verify behavior preserved (no regression)
    Tool: Bash (diff)
    Preconditions: Baseline captured before changes
    Steps:
      1. ./c_tester tests/*.c > /tmp/after_fix1.txt 2>&1
      2. diff /tmp/baseline.txt /tmp/after_fix1.txt
    Expected Result: diff exits with code 0 (no differences)
    Failure Indicators: Any output from diff command
    Evidence: .sisyphus/evidence/task-1-diff.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-1-valgrind-clean.txt
  - [ ] .sisyphus/evidence/task-1-diff.txt

  **Commit**: NO (group with Wave 1 fixes)

---

- [x] 2. Fix resource leaks in run_with_valgrind() error paths

  **What to do**:
  - Read c_tester.c lines 544-685 (run_with_valgrind function)
  - Locate error paths where fork() fails (lines 563-566) and pipes were opened but not closed
  - On fork() failure: close(stdout_pipe[0]), close(stderr_pipe[0]) before returning -1
  - Verify all error paths in the select() loop also close pipe FDs properly
  - Rebuild with `gcc -Wall -Wextra -Werror -fsanitize=address,undefined -o c_tester c_tester.c`

  **Must NOT do**:
  - Do NOT change the execvp() logic or valgrind arguments
  - Do NOT modify the select() timeout logic

  **Recommended Agent Profile**:
  > Category: `quick`
  > Reason: Same pattern as Task 1 - simple FD cleanup on error paths.
  > Skills: None needed.

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 1, 3, 4)
  - **Parallel Group**: Wave 1
  - **Blocks**: Task 5, 6, 7
  - **Blocked By**: None

  **References**:
  - `c_tester.c:544-685` - run_with_valgrind() function
  - `c_tester.c:559-566` - Pipe creation and fork() failure paths (THE BUG)
  - `c_tester.c:408-411` - Same bug pattern in run_binary() (fix first, use as reference)

  **Acceptance Criteria**:
  - [ ] c_tester.c compiles with `-Wall -Wextra -Werror`
  - [ ] Resource leaks fixed: all error paths close pipe FDs
  - [ ] Run `valgrind --track-fds=yes ./c_tester --valgrind tests/valgrind_uninit.c` → No pipe leaks

  **QA Scenarios**:

  ```
  Scenario: Verify resource leaks fixed in valgrind path
    Tool: Bash (valgrind)
    Preconditions: valgrind installed, c_tester compiled with sanitizers
    Steps:
      1. valgrind --track-fds=yes ./c_tester --valgrind tests/valgrind_uninit.c 2>&1 | tee /tmp/valgrind_test2.txt
      2. grep -q "Open file descriptor.*pipe" /tmp/valgrind_test2.txt
    Expected Result: grep exits with code 1 (no pipe leaks found)
    Evidence: .sisyphus/evidence/task-2-valgrind-fd-check.txt

  Scenario: Behavior preservation for valgrind tests
    Tool: Bash (c_tester)
    Steps:
      1. ./c_tester --valgrind tests/valgrind_uninit.c > /tmp/after_fix2.txt 2>&1
      2. grep -q "Uninitialized" /tmp/after_fix2.txt
    Expected Result: grep exits 0 (detects uninitialized variable)
    Evidence: .sisyphus/evidence/task-2-behavior.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-2-valgrind-fd-check.txt
  - [ ] .sisyphus/evidence/task-2-behavior.txt

  **Commit**: NO (group with Wave 1 fixes)

---

- [x] 3. Fix generate_temp_path() race condition

  **What to do**:
  - Read c_tester.c lines 687-706 (generate_temp_path function)
  - Current code: mkstemp(buffer) creates file, then close(fd) leaves file on disk
  - Fix: After mkstemp + close, immediately call unlink(buffer) to remove the file
  - This prevents race condition where another process could open/modify the temp file
  - Rebuild with `gcc -Wall -Wextra -Werror -fsanitize=address,undefined -o c_tester c_tester.c`
  - Verify with `strace -e unlink ./c_tester tests/clean.c 2>&1 | grep unlink`

  **Must NOT do**:
  - Do NOT change the temp file naming scheme
  - Do NOT remove the mkstemp() call itself

  **Recommended Agent Profile**:
  > Category: `quick`
  > Reason: Simple fix - add unlink() after mkstemp. Well-known pattern.
  > Skills: None needed.

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 1, 2, 4)
  - **Parallel Group**: Wave 1
  - **Blocks**: Task 5, 6, 7
  - **Blocked By**: None

  **References**:
  - `c_tester.c:687-706` - generate_temp_path() function (THE BUG)
  - `man 3 mkstemp` - mkstemp documentation (should unlink after creation)
  - `AGENTS.md` "Resource cleanup" section - proper temp file handling

  **Acceptance Criteria**:
  - [ ] c_tester.c compiles with `-Wall -Wextra -Werror`
  - [ ] generate_temp_path() calls unlink() after mkstemp + close
  - [ ] Race condition fixed: temp file doesn't persist on disk
  - [ ] strace shows unlink() being called for temp file

  **QA Scenarios**:

  ```
  Scenario: Verify unlink() called after mkstemp
    Tool: Bash (strace)
    Preconditions: c_tester compiled with sanitizers
    Steps:
      1. strace -e unlink ./c_tester tests/clean.c 2>&1 | grep -E "unlink.*c_tester_.*XXXXXX" | tee /tmp/strace_unlink.txt
    Expected Result: unlink() call visible in strace output for temp file path
    Failure Indicators: No unlink calls for c_tester temp files
    Evidence: .sisyphus/evidence/task-3-strace-unlink.txt

  Scenario: Verify temp files don't persist after execution
    Tool: Bash (ls)
    Steps:
      1. ls /tmp/c_tester_* 2>/dev/null | wc -l
      2. ./c_tester tests/clean.c
      3. ls /tmp/c_tester_* 2>/dev/null | wc -l
    Expected Result: Line counts are the same (no new temp files created)
    Evidence: .sisyphus/evidence/task-3-ls-tmp.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-3-strace-unlink.txt
  - [ ] .sisyphus/evidence/task-3-ls-tmp.txt

  **Commit**: NO (group with Wave 1 fixes)

---

- [x] 4. Minimal signal safety improvements

  **What to do**:
  - Review c_tester.c for variables that should be `volatile` (shared between signal handlers and main)
  - In run_binary() and run_with_valgrind(), check if child_pid should be volatile
  - Add comments documenting signal safety decisions (why volatile is/isn't needed)
  - If child_pid is passed to caller via pointer, ensure it's written before fork() returns
  - Rebuild with `gcc -Wall -Wextra -Werror -fsanitize=address,undefined -o c_tester c_tester.c`

  **Must NOT do**:
  - Do NOT add signal handlers if none exist
  - Do NOT restructure signal architecture
  - Do NOT add volatile to variables that don't need it

  **Recommended Agent Profile**:
  > Category: `quick`
  > Reason: Minimal review and possibly adding volatile keyword. No deep refactoring.
  > Skills: `debuggers/gdb` (optional, for understanding signal behavior)

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 1, 2, 3)
  - **Parallel Group**: Wave 1
  - **Blocks**: Task 5, 6, 7
  - **Blocked By**: None

  **References**:
  - `c_tester.c:389-526` - run_binary() (child_pid parameter usage)
  - `c_tester.c:544-685` - run_with_valgrind() (child_pid parameter usage)
  - `man 7 signal-safety` - Signal safety documentation
  - `AGENTS.md` "Thread-safety: Single-threaded tool" - Current design note

  **Acceptance Criteria**:
  - [ ] c_tester.c compiles with `-Wall -Wextra -Werror`
  - [ ] Signal safety reviewed, volatile added only where needed
  - [ ] Comments added explaining signal safety decisions
  - [ ] No new signal handlers added (minimal approach)

  **QA Scenarios**:

  ```
  Scenario: Verify tool still works after signal safety changes
    Tool: Bash (c_tester)
    Steps:
      1. ./c_tester tests/null_deref.c > /tmp/after_fix4.txt 2>&1
      2. grep -q "NULL Pointer" /tmp/after_fix4.txt
    Expected Result: grep exits 0 (detects null dereference)
    Evidence: .sisyphus/evidence/task-4-null-deref.txt

  Scenario: Verify timeout still works (signal-related)
    Tool: Bash (c_tester)
    Steps:
      1. timeout 5 ./c_tester tests/infinite_loop.c > /tmp/after_fix4_timeout.txt 2>&1
      2. grep -q "timeout\|TERM\|KILL" /tmp/after_fix4_timeout.txt
    Expected Result: grep exits 0 (timeout/termination detected)
    Evidence: .sisyphus/evidence/task-4-timeout.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-4-null-deref.txt
  - [ ] .sisyphus/evidence/task-4-timeout.txt

  **Commit**: NO (group with Wave 1 fixes)

---

- [ ] 5. Extract run_with_timeout() helper function

  **What to do**:
  - Analyze run_binary() (lines 389-526) and run_with_valgrind() (lines 544-685)
  - Identify shared logic: pipe creation, fork, select() timeout loop, read loops, cleanup
  - Extract into new function: `run_with_timeout(const char *binary, char *const argv[], char *output, size_t output_size, char *error_output, size_t error_size, int timeout_sec, pid_t *child_pid)`
  - Move shared code: pipe(), fork(), parent's select() loop, read loops, cleanup
  - Leave binary-specific code (execvp args) in original functions
  - Rebuild with `gcc -Wall -Wextra -Werror -fsanitize=address,undefined -o c_tester c_tester.c`

  **Must NOT do**:
  - Do NOT change behavior of run_binary() or run_with_valgrind()
  - Do NOT modify execvp() arguments or valgrind-specific logic
  - Do NOT break error handling or timeout behavior

  **Recommended Agent Profile**:
  > Category: `deep`
  > Reason: Requires thorough understanding of both functions to extract correct shared logic without breaking behavior.
  > Skills: `debuggers/gdb` (optional, for understanding fork/exec flow)

  **Parallelization**:
  - **Can Run In Parallel**: NO (depends on Wave 1 bug fixes)
  - **Parallel Group**: Wave 2 (with tasks 6, 7)
  - **Blocks**: Task 6, 7 (they will use the new helper)
  - **Blocked By**: Tasks 1, 2, 3, 4 (bug fixes must be done first)

  **References**:
  - `c_tester.c:389-526` - run_binary() (SOURCE to extract from)
  - `c_tester.c:544-685` - run_with_valgrind() (SOURCE to extract from)
  - `c_tester.h:182-185` - run_binary() declaration (may need update)
  - `c_tester.h:202-205` - run_with_valgrind() declaration (may need update)
  - `AGENTS.md` "Function Size" section - Keep functions under 40 lines

  **Acceptance Criteria**:
  - [ ] New `run_with_timeout()` function created
  - [ ] Shared logic extracted: pipe creation, fork, select() loop, read loops, cleanup
  - [ ] `run_binary()` and `run_with_valgrind()` now call `run_with_timeout()`
  - [ ] c_tester.c compiles with `-Wall -Wextra -Werror`
  - [ ] Behavior preserved (see QA scenarios)

  **QA Scenarios**:

  ```
  Scenario: Verify behavior preserved after refactoring (run_binary)
    Tool: Bash (diff)
    Preconditions: Baseline captured before changes
    Steps:
      1. ./c_tester tests/null_deref.c tests/buffer_overflow.c tests/use_after_free.c > /tmp/after_refactor.txt 2>&1
      2. diff /tmp/baseline.txt /tmp/after_refactor.txt
    Expected Result: diff exits 0 (no differences)
    Failure Indicators: Any output from diff (behavior changed)
    Evidence: .sisyphus/evidence/task-5-diff-run_binary.txt

  Scenario: Verify behavior preserved (run_with_valgrind)
    Tool: Bash (c_tester + valgrind)
    Steps:
      1. ./c_tester --valgrind tests/valgrind_uninit.c > /tmp/after_valgrind.txt 2>&1
      2. grep -q "Uninitialized" /tmp/after_valgrind.txt
    Expected Result: grep exits 0 (still detects error)
    Evidence: .sisyphus/evidence/task-5-valgrind-test.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-5-diff-run_binary.txt
  - [ ] .sisyphus/evidence/task-5-valgrind-test.txt

  **Commit**: NO (group with Wave 2 refactoring)

---

- [x] 6. Refactor run_binary() to use run_with_timeout() helper

  **What to do**:
  - Modify run_binary() (lines 389-526) to call run_with_timeout()
  - Pass correct argv: `{binary, args, NULL}` to the helper
  - Helper handles: pipe(), fork(), select() loop, read(), cleanup
  - run_binary() only needs to: set up argv, call helper, waitpid(), return status
  - Should reduce run_binary() from ~140 lines to ~30 lines
  - Rebuild and verify

  **Must NOT do**:
  - Do NOT change the public API of run_binary()
  - Do NOT modify how argv is passed to the binary

  **Recommended Agent Profile**:
  > Category: `deep`
  > Reason: Depends on Task 5 - needs to correctly use the new helper.
  > Skills: None needed (straightforward refactoring after Task 5).

  **Parallelization**:
  - **Can Run In Parallel**: NO (depends on Task 5)
  - **Parallel Group**: Wave 2 (after Task 5 completes)
  - **Blocks**: Task F1-F4 (final verification)
  - **Blocked By**: Task 5

  **References**:
  - `c_tester.c:389-526` - run_binary() (TO REFACTOR)
  - Task 5 output - run_with_timeout() (USE THIS)
  - `c_tester.h:182-185` - run_binary() declaration (API unchanged)

  **Acceptance Criteria**:
  - [ ] run_binary() now calls run_with_timeout()
  - [ ] run_binary() reduced to ~30 lines (from ~140)
  - [ ] Compiles with `-Wall -Wextra -Werror`
  - [ ] All existing tests pass

  **QA Scenarios**:

  ```
  Scenario: Verify run_binary() works after refactoring
    Tool: Bash (make test)
    Steps:
      1. cd /home/jadkeskes/c_tester && make test 2>&1 | tee /tmp/make_test.txt
      2. grep -q "FAIL" /tmp/make_test.txt
    Expected Result: grep exits 1 (no FAIL found)
    Evidence: .sisyphus/evidence/task-6-make-test.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-6-make-test.txt

  **Commit**: NO (group with Wave 2 refactoring)

---

- [ ] 7. Refactor run_with_valgrind() to use run_with_timeout() helper

  **What to do**:
  - Modify run_with_valgrind() (lines 544-685) to call run_with_timeout()
  - Pass correct argv: `{"valgrind", "--leak-check=full", "--show-leak-kinds=all", "--track-origins=yes", "--error-exitcode=1", binary, NULL}`
  - Helper handles: pipe(), fork(), select() loop, read(), cleanup
  - run_with_valgrind() only needs to: set up argv for valgrind, call helper, waitpid(), return status
  - Should reduce run_with_valgrind() from ~140 lines to ~30 lines
  - Rebuild and verify

  **Must NOT do**:
  - Do NOT change valgrind arguments
  - Do NOT modify how valgrind output is captured

  **Recommended Agent Profile**:
  > Category: `deep`
  > Reason: Similar to Task 6, depends on Task 5 helper.
  > Skills: None needed.

  **Parallelization**:
  - **Can Run In Parallel**: NO (depends on Task 5)
  - **Parallel Group**: Wave 2 (after Task 5 completes)
  - **Blocks**: Task F1-F4 (final verification)
  - **Blocked By**: Task 5

  **References**:
  - `c_tester.c:544-685` - run_with_valgrind() (TO REFACTOR)
  - Task 5 output - run_with_timeout() (USE THIS)
  - `c_tester.h:202-205` - run_with_valgrind() declaration (API unchanged)

  **Acceptance Criteria**:
  - [ ] run_with_valgrind() now calls run_with_timeout()
  - [ ] run_with_valgrind() reduced to ~30 lines (from ~140)
  - [ ] Compiles with `-Wall -Wextra -Werror`
  - [ ] Valgrind tests pass

  **QA Scenarios**:

  ```
  Scenario: Verify run_with_valgrind() works after refactoring
    Tool: Bash (make test with valgrind)
    Steps:
      1. cd /home/jadkeskes/c_tester && make test 2>&1 | grep -A2 "Valgrind"
    Expected Result: "PASS (error detected)" for valgrind test
    Evidence: .sisyphus/evidence/task-7-valgrind-test.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-7-valgrind-test.txt

  **Commit**: YES (Wave 2 complete - refactoring done)
  - Message: `refactor: extract run_with_timeout() helper, reduce code duplication`
  - Files: `c_tester.c`, `c_tester.h`
  - Pre-commit: `make test`

---

- [ ] 8. Add test cases for all error patterns (glibc-style)

  **What to do**:
  - Review `error_patterns[]` table in c_tester.c (lines 831-1008)
  - Count total error patterns: ~70 patterns
  - For each pattern, ensure there's a test case in `tests/` or `bad_codes_v2/`
  - Create missing test files for patterns without test coverage
  - Each test should: trigger the specific error, be compilable with `-fsanitize=...`
  - Use glibc-style: test name describes what it tests (e.g., `test_uaf_simple.c`)
  - Add test to Makefile `test` target if it's a core test

  **Must NOT do**:
  - Do NOT modify c_tester.c error detection logic
  - Do NOT create test that doesn't trigger the intended error

  **Recommended Agent Profile**:
  > Category: `unspecified-high`
  > Reason: Need to analyze 70+ error patterns, create test cases, verify each triggers correctly.
  > Skills: None needed (creating C test files).

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 9, 10)
  - **Parallel Group**: Wave 3
  - **Blocks**: Task F1-F4 (final verification)
  - **Blocked By**: Tasks 5, 6, 7 (refactoring must be done first)

  **References**:
  - `c_tester.c:831-1008` - error_patterns[] table (ALL patterns to test)
  - `tests/*.c` - Existing test files (reference for style)
  - `bad_codes_v2/*.c` - Additional bad code tests (reference)
  - `Makefile:18-54` - test target (where to add new tests)

  **Acceptance Criteria**:
  - [ ] All ~70 error patterns have at least one test case
  - [ ] Each test file compiles with `-fsanitize=address,undefined`
  - [ ] Each test triggers the expected error when run through c_tester
  - [ ] Tests organized like glibc: `test_<error_type>_<variant>.c`

  **QA Scenarios**:

  ```
  Scenario: Verify all error patterns have test coverage
    Tool: Bash (c_tester)
    Steps:
      1. grep -c "^{" c_tester.c | head -1  # Count patterns
      2. ls tests/*.c bad_codes_v2/*.c | wc -l
      3. Compare: enough tests for all patterns
    Expected Result: At least one test per error pattern
    Evidence: .sisyphus/evidence/task-8-coverage.txt

  Scenario: Verify new tests detect errors correctly
    Tool: Bash (make test)
    Steps:
      1. cd /home/jadkeskes/c_tester && make test 2>&1 | tee /tmp/make_test8.txt
      2. grep -c "PASS" /tmp/make_test8.txt
    Expected Result: All tests show PASS (error detected)
    Evidence: .sisyphus/evidence/task-8-make-test.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-8-coverage.txt
  - [ ] .sisyphus/evidence/task-8-make-test.txt

  **Commit**: NO (group with Wave 3 test suite upgrade)

---

- [x] 9. Add edge case tests (large output, timeout, etc.)

  **What to do**:
  - Create test for large output (> pipe buffer size, ~4KB+)
  - Create test for program with no output (empty stdout)
  - Create test for program outputting exactly 4096 bytes (pipe buffer edge)
  - Create test for rapid successive allocations (stress memory)
  - Create test for deep recursion (stack overflow edge)
  - Create test for very long filename/path (MAX_PATH_LEN edge)
  - Add to Makefile `test-all` target

  **Must NOT do**:
  - Do NOT create tests that hang indefinitely (use timeout)
  - Do NOT create tests that crash the test runner itself

  **Recommended Agent Profile**:
  > Category: `unspecified-high`
  > Reason: Need to create various edge case tests, verify they work correctly.
  > Skills: None needed.

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 8, 10)
  - **Parallel Group**: Wave 3
  - **Blocks**: Task F1-F4
  - **Blocked By**: Tasks 5, 6, 7

  **References**:
  - `c_tester.c:389-526` - run_binary() (timeout logic to test)
  - `c_tester.c:MAX_OUTPUT_SIZE` - Output buffer size (64KB)
  - `AGENTS.md` "Edge Case Obsession" section - Philosophy for edge cases

  **Acceptance Criteria**:
  - [ ] Large output test: program outputs > 64KB, c_tester handles it
  - [ ] Empty output test: program outputs nothing, c_tester completes
  - [ ] Path length test: very long path, c_tester handles gracefully
  - [ ] All edge case tests pass with `make test-all`

  **QA Scenarios**:

  ```
  Scenario: Test large output handling (>64KB)
    Tool: Bash (c_tester)
    Steps:
      1. Create test that prints 100KB of data
      2. ./c_tester tests/large_output.c > /tmp/large_test.txt 2>&1
      3. Check output captured correctly
    Expected Result: c_tester doesn't truncate or crash
    Evidence: .sisyphus/evidence/task-9-large-output.txt

  Scenario: Test empty output handling
    Tool: Bash (c_tester)
    Steps:
      1. ./c_tester tests/empty_output.c > /tmp/empty_test.txt 2>&1
      2. grep -q "No errors detected" /tmp/empty_test.txt
    Expected Result: grep exits 0 (clean run detected)
    Evidence: .sisyphus/evidence/task-9-empty-output.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-9-large-output.txt
  - [ ] .sisyphus/evidence/task-9-empty-output.txt

  **Commit**: NO (group with Wave 3 test suite upgrade)

---

- [ ] 10. Enhance Makefile test targets with Valgrind and sanitizer verification

  **What to do**:
  - Add `test-valgrind` target: runs c_tester with --valgrind flag on all tests
  - Add `test-sanitizers` target: runs with ASan/UBSan, verifies no sanitizer errors
  - Add `test-leaks` target: runs with `valgrind --leak-check=full --track-fds=yes`
  - Enhance `test` target: add `--leak-check=full` verification step
  - Add `test-all-verbose` target: shows detailed output for each test
  - Ensure all targets use `set -e` for proper error propagation

  **Must NOT do**:
  - Do NOT remove existing test targets
  - Do NOT add dependencies that aren't available (check for valgrind first)

  **Recommended Agent Profile**:
  > Category: `quick`
  > Reason: Makefile editing - straightforward, no deep analysis needed.
  > Skills: None needed.

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 8, 9)
  - **Parallel Group**: Wave 3
  - **Blocks**: Task F1-F4
  - **Blocked By**: Tasks 5, 6, 7

  **References**:
  - `Makefile` - Current Makefile (MODIFY THIS)
  - `Makefile:18-54` - Current `test` target (ENHANCE)
  - `Makefile:64-82` - Current `test-all` target (ENHANCE)
  - `AGENTS.md` "Test Execution Policy" section - Guidelines for test targets

  **Acceptance Criteria**:
  - [ ] `make test-valgrind` target works (if valgrind installed)
  - [ ] `make test-sanitizers` target added
  - [ ] `make test-leaks` target added (valgrind leak check)
  - [ ] Existing targets still work (backward compatible)

  **QA Scenarios**:

  ```
  Scenario: Verify new test-valgrind target
    Tool: Bash (make)
    Steps:
      1. cd /home/jadkeskes/c_tester && make test-valgrind 2>&1 | tee /tmp/test_valgrind.txt
      2. grep -E "PASS|FAIL" /tmp/test_valgrind.txt
    Expected Result: All tests PASS (or SKIP if valgrind not installed)
    Evidence: .sisyphus/evidence/task-10-test-valgrind.txt

  Scenario: Verify test-leaks target (memory leak detection)
    Tool: Bash (make)
    Steps:
      1. make test-leaks 2>&1 | tee /tmp/test_leaks.txt
      2. grep -q "definitely lost: 0 bytes" /tmp/test_leaks.txt
    Expected Result: No memory leaks detected in c_tester itself
    Evidence: .sisyphus/evidence/task-10-test-leaks.txt
  ```

  **Evidence to Capture:**
  - [ ] .sisyphus/evidence/task-10-test-valgrind.txt
  - [ ] .sisyphus/evidence/task-10-test-leaks.txt

  **Commit**: YES (Wave 3 complete - test suite upgraded)
  - Message: `test: upgrade test suite to glibc/open-source standards`
  - Files: `Makefile`, `tests/*.c`, `bad_codes_v2/*.c`
  - Pre-commit: `make test && make test-all`

---

## Final Verification Wave (MANDATORY — after ALL implementation tasks)

> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.
>
> **Do NOT auto-proceed after verification. Wait for user's explicit approval before marking work complete.**
> **Never mark F1-F4 as checked before getting user's okay.** Rejection or user feedback -> fix -> re-run -> present again -> wait for okay.

- [ ] F1. **Plan Compliance Audit** — `oracle`
  Read the plan end-to-end. For each "Must Have": verify implementation exists (read file, run command). For each "Must NOT Have": search codebase for forbidden patterns — reject with file:line if found. Check evidence files exist in .sisyphus/evidence/. Compare deliverables against plan.
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

- [ ] F2. **Code Quality Review** — `unspecified-high`
  Run `gcc -Wall -Wextra -Werror -fsanitize=address,undefined -o c_tester c_tester.c`. Review all changed files for: `as any`/`@ts-ignore`, empty catches, console.log in prod, commented-out code, unused imports. Check AI slop: excessive comments, over-abstraction, generic names (data/result/item/temp).
  Output: `Build [PASS/FAIL] | Lint [PASS/FAIL] | Tests [N pass/N fail] | Files [N clean/N issues] | VERDICT`

- [ ] F3. **Real Manual QA** — `unspecified-high` (+ `valgrind` if available)
  Start from clean state. Execute EVERY QA scenario from EVERY task — follow exact steps, capture evidence. Test cross-task integration (features working together, not isolation). Test edge cases: empty state, invalid input, rapid actions. Save to `.sisyphus/evidence/final-qa/`.
  Output: `Scenarios [N/N pass] | Integration [N/N] | Edge Cases [N tested] | VERDICT`

- [x] F4. **Scope Fidelity Check** — `deep`
  For each task: read "What to do", read actual diff (git log/diff). Verify 1:1 — everything in spec was built (no missing), nothing beyond spec was built (no creep). Check "Must NOT do" compliance. Detect cross-task contamination: Task N touching Task M's files. Flag unaccounted changes.
  Output: `Tasks [N/N compliant] | Contamination [CLEAN/N issues] | Unaccounted [CLEAN/N files] | VERDICT`

---

## Commit Strategy

| Wave | Commit Message | Files | Pre-commit |
|------|----------------|-------|-------------|
| 1 | `fix: resource leaks and race condition in c_tester` | `c_tester.c` | `make test` |
| 2 | `refactor: extract run_with_timeout() helper, reduce duplication` | `c_tester.c`, `c_tester.h` | `make test` |
| 3 | `test: upgrade test suite to glibc/open-source standards` | `Makefile`, `tests/*.c`, `bad_codes_v2/*.c` | `make test && make test-all` |

---

## Success Criteria

### Verification Commands
```bash
cd /home/jadkeskes/c_tester && \
gcc -Wall -Wextra -Werror -fsanitize=address,undefined -o c_tester c_tester.c && \
echo "Build: PASS" && \
make test && \
echo "Test: PASS" && \
make test-all && \
echo "Test-All: PASS" && \
valgrind --track-fds=yes --leak-check=full ./c_tester tests/clean.c 2>&1 | grep -q "definitely lost: 0 bytes" && \
echo "Leak Check: PASS"
```

### Final Checklist
- [ ] All "Must Have" present (resource leaks fixed, race condition fixed, refactoring done, test suite upgraded)
- [ ] All "Must NOT Have" absent (no unit tests for tool itself, no API changes, no new features)
- [ ] All tests pass (`make test`, `make test-all`)
- [ ] No memory leaks (valgrind verification)
- [ ] No undefined behavior (UBSan verification)
- [ ] Behavior preserved (diff of baseline vs after shows no changes)
- [ ] All evidence files captured in `.sisyphus/evidence/`






