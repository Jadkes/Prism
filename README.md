# prism

throws gcc, clang, asan, ubsan, tsan, valgrind, clang-tidy, libfuzzer,
and whatever else at your C code until something breaks.

```
cmake -B build
cmake --build build
./build/prism --ultra my_code.c
```

## what it does

compiles your source a bunch of times with different flags and runs
every analysis tool it can find. catches null derefs, use-after-free,
double free, buffer overflows, div by zero, uninitialized vars, integer
overflows, data races, shift bugs, negative indexing, resource leaks,
and dangerous api calls.

## modes

`--quick` — just gcc warnings
`--full` — asan + ubsan
`--ultra` — the big one. 11 compiles, 24+ analysis passes in parallel.
`--tsan` — thread sanitizer
`--analyzer` — gcc -fanalyzer
`--clang-tidy` — clang-tidy
`--valgrind` — valgrind memcheck (slow but worth it)
`--fuzz` — boundary fuzz (empty, huge, negative inputs)
`--rerun=N` — run N times, catch heisenbugs
`--resources` — check file descriptor leaks
`--danger` — grep for sprintf/strcpy/etc
`--ast` — libclang ast analysis (no regex false positives)
`--gcov` — code coverage
`--libfuzzer` — actual libfuzzer
`--gdb` — auto backtrace on crash

`--git-diff` — only check changed files
`--git-bisect=<ref>` — find the commit that broke it
`--install-hook` — pre-commit hook so you can't ship broken shit

## build

```bash
cmake -B build
cmake --build build
sudo cmake --install build
```

needs cmake 3.16+, gcc or clang, and libclang headers for `--ast` mode
(`dnf install clang-devel` / `apt install libclang-dev`).

valgrind is optional but you should have it.

## files

```
prism.c          — main thing
prism.h          — types and decls
check_engine.c      — correctness checks (8 modules)
sym_exec.c          — symbolic execution for integer paths
ast_backend.c       — libclang ast stuff
detect_libs.c       — auto-detect -l flags from #includes
annotation.c        — annotation parsing
```

hell_code.c in the repo root has 15 different bugs if you want to
see it work.

## license

gpl v2.
