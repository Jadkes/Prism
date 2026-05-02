# C Tester v3 - Security, Quality & Features

## TODOs

- [ ] V3-1: Security - Replace popen() with fork/execvp for compile calls
- [ ] V3-2: Code Quality - Refactor main() to <40 lines
- [ ] V3-3: Code Quality - Refactor run_binary() and run_with_valgrind() to <40 lines
- [ ] V3-4: Code Quality - Refactor generate_html_report() to <40 lines
- [ ] V3-5: Feature - Add --json output mode for CI/CD
- [ ] V3-6: Feature - Add --ubsan standalone flag
- [ ] V3-7: Edge Cases - Add vla-bound and pointer-overflow UBSan patterns

## Dependencies

- V3-1: Independent (security fix)
- V3-2, V3-3, V3-4: Independent refactors (can parallel)
- V3-5, V3-6, V3-7: Independent features (can parallel)
- All depend on clean build after each change

## Final Verification Wave

- [ ] F1: Code Quality Review - Verify all functions <40 lines, naming conventions
- [ ] F2: Security Review - Verify no popen() remains, no shell injection
- [ ] F3: QA Testing - All tests pass, 0 warnings
- [ ] F4: Context Mining - Verify conventions maintained
