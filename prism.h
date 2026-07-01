/*
 * prism.h - Header file for prism, a simple C/C++ error detection tool
 *
 * Provides error detection, memory leak detection, and debugging
 *          capabilities that are simpler than GDB and catch more errors
 *          than standard compilation. Supports both C and C++ sources.
 *
 * Wraps GCC/G++ compilation with ASan/UBSan sanitizers, falls back to
 *         signal-based detection if sanitizers are unavailable. Parses error
 *         output and provides human-readable fix suggestions.
 */

#ifndef PRISM_H
#define PRISM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>

/* Constants */
#define MAX_LINE_LEN        2048
#define MAX_OUTPUT_SIZE     (64 * 1024)
#define MAX_SOURCE_LINE     512
#define MAX_PATH_LEN        4096
#define EXIT_CLEAN          0
#define EXIT_ERRORS_FOUND   1
#define EXIT_COMPILE_FAIL   2
#define EXIT_FILE_NOT_FOUND 3
#define EXIT_USAGE_ERROR    4
#define DEFAULT_TIMEOUT_SEC 30

/* Error types detected by prism */
typedef enum {
    ERR_NULL_DEREF,
    ERR_BUFFER_OVERFLOW,
    ERR_USE_AFTER_FREE,
    ERR_MEMORY_LEAK,
    ERR_INT_OVERFLOW,
    ERR_DIV_BY_ZERO,
    ERR_UNINIT_VAR,
    ERR_STACK_OVERFLOW,
    ERR_SEG,
    ERR_ABORT,
    ERR_DATA_RACE,
    ERR_BAD_ALLOC,
    ERR_OUT_OF_RANGE,
    ERR_LOGIC_ERROR,
    ERR_PURE_VIRTUAL,
    ERR_DOUBLE_FREE_CPP,
    ERR_HARDENING,
    ERR_STRICT_ALIASING,
    ERR_CONVERSION_LOSS,
    ERR_FLOAT_COMPARE,
    ERR_UNKNOWN,
    ERR_NONE
} ErrorType;

/* Represents a detected error with context and fix suggestion */
typedef struct {
    ErrorType type;
    char title[256];
    char fix_suggestion[1024];
    char source_file[MAX_PATH_LEN];
    int source_line;
    int severity;
    bool has_source;
} DetectedError;

/* Result of compiling and running a test file */
typedef struct {
    bool compilation_success;
    int exit_code;
    int signal_received;
    char compiler_output[MAX_OUTPUT_SIZE];
    char runtime_output[MAX_OUTPUT_SIZE];
    char sanitizer_output[MAX_OUTPUT_SIZE];
    long execution_time_ms;
} TestResult;

/* Pattern to match in error output for classification */
typedef struct {
    const char *pattern;
    const char *title;
    const char *fix;
    ErrorType type;
    int severity;
} ErrorPattern;

/* ANSI color codes for terminal output */
typedef struct {
    const char *red;
    const char *green;
    const char *yellow;
    const char *blue;
    const char *magenta;
    const char *cyan;
    const char *bold;
    const char *reset;
} ColorCodes;

/* Initialize color codes based on whether colors are enabled */
void init_colors(ColorCodes *colors, bool use_colors);

/* Print formatted message with color */
void print_colored(const ColorCodes *colors, const char *color, const char *fmt, ...);

/* Print banner showing file(s) being tested */
void print_banner(const ColorCodes *colors, const char **source_files, int source_count);

/* Print summary of test results */
void print_summary(const ColorCodes *colors, const TestResult *result, int error_count, const DetectedError *errors);

/*
 * compile_with_sanitizers - Compile with ASan+UBSan (GCatch memory errors + UB)
 * Supports multiple source files compiled into a single binary.
 */
int compile_with_sanitizers(const char **sources, int source_count,
                             const char *binary,
                             char *output, size_t output_size);

/*
 * compile_with_msan - Compile with MemorySanitizer
 */
int compile_with_msan(const char **sources, int source_count,
                       const char *binary,
                       char *output, size_t output_size);

/*
 * compile_with_tsan - Compile source files with ThreadSanitizer
 */
int compile_with_tsan(const char **sources, int source_count,
                       const char *binary,
                       char *output, size_t output_size);

/*
 * compile_fallback - Compile without sanitizers as fallback
 */
int compile_fallback(const char **sources, int source_count,
                      const char *binary,
                      char *output, size_t output_size);

/*
 * compile_for_warnings - Compile with warning flags only (no sanitizers)
 */
int compile_for_warnings(const char **sources, int source_count,
                         char *output, size_t output_size);

/*
 * compile_for_valgrind - Compile for Valgrind analysis (-O0 -g)
 */
int compile_for_valgrind(const char **sources, int source_count,
                         const char *binary, char *output, size_t output_size);

/*
 * compile_with_analyzer - Compile with GCC static analyzer (-fanalyzer)
 */
int compile_with_analyzer(const char **sources, int source_count,
                        const char *binary,
                        char *output, size_t output_size);

/*
 * compile_with_clang_tidy - Run clang-tidy static analysis
 */
int compile_with_clang_tidy(const char **sources, int source_count,
                            char *output, size_t output_size);

/*
 * compile_with_strict_aliasing - Detect type-punning via -Wstrict-aliasing=1
 */
int compile_with_strict_aliasing(const char **sources, int source_count,
                                  char *output, size_t output_size);

/*
 * compile_with_float_equal_warning - Catch floating-point ==/!= via -Wfloat-equal
 */
int compile_with_float_equal_warning(const char **sources, int source_count,
                                      char *output, size_t output_size);

/*
 * compile_with_conversion_warnings - Catch implicit truncation via -Wconversion
 */
int compile_with_conversion_warnings(const char **sources, int source_count,
                                      char *output, size_t output_size,
                                      bool include_sign_conversion);

/*
 * run_cppcheck - Run cppcheck static analysis on source files
 */
int run_cppcheck(const char **sources, int source_count,
                  char *output, size_t output_size);

/*
 * run_clang_analyze - Run Clang Static Analyzer on source files
 */
int run_clang_analyze(const char **sources, int source_count,
                       char *output, size_t output_size);

/*
 * run_iwyu - Run include-what-you-use on source files
 */
int run_iwyu(const char **sources, int source_count,
              char *output, size_t output_size);

/*
 * compile_cpp_with_analyzer - Compile C++ with GCC static analyzer (-fanalyzer)
 */
int compile_cpp_with_analyzer(const char **sources, int source_count,
                          const char *binary,
                          char *output, size_t output_size);

/*
 * compile_cpp_with_sanitizers - Compile C++ sources with ASan and UBSan
 */
int compile_cpp_with_sanitizers(const char **sources, int source_count,
                                 const char *binary,
                                 char *output, size_t output_size);

/*
 * compile_cpp_with_tsan - Compile C++ sources with ThreadSanitizer
 */
int compile_cpp_with_tsan(const char **sources, int source_count,
                           const char *binary,
                           char *output, size_t output_size);

/*
 * compile_cpp_for_warnings - Compile C++ with warning flags only
 */
int compile_cpp_for_warnings(const char **sources, int source_count,
                                  char *output, size_t output_size);

/*
 * run_with_timeout - Execute a binary with timeout, capturing output
 */
int run_with_timeout(const char *binary, char *const argv[],
                    char *output, size_t output_size,
                    char *error_output, size_t error_size,
                    int timeout_sec, pid_t *child_pid);

/*
 * run_binary - Execute compiled binary with timeout
 */
int run_binary(const char *binary, const char *args,
               char *output, size_t output_size,
               char *error_output, size_t error_size,
               int timeout_sec, pid_t *child_pid);

/*
 * run_with_valgrind - Execute binary under Valgrind for memory error detection
 */
int run_with_valgrind(const char *binary,
                      char *output, size_t output_size,
                      char *error_output, size_t error_size,
                      int timeout_sec, pid_t *child_pid);

/*
 * classify_error - Determine error pattern index from error line
 */
int classify_error(const char *error_line);

/*
 * parse_sanitizer_errors - Extract errors from sanitizer output
 */
int parse_sanitizer_errors(const char *error_output,
                            DetectedError *errors, int max_errors);

/*
 * parse_compile_commands - Extract compile flags for a source file
 */
int parse_compile_commands(const char *json_path, const char *source_file,
                          char *flags_output, size_t flags_size);

/*
 * run_with_compile_flags - Compile using flags from compile_commands.json
 */
int run_with_compile_flags(const char **sources, int source_count,
                           const char *flags,
                           const char *binary,
                           char *output, size_t output_size);

/*
 * generate_fix_suggestion - Create fix suggestion for detected error
 */
void generate_fix_suggestion(DetectedError *error);

/* Get human-readable name for error type */
const char *get_error_name(ErrorType type);

/* Map ErrorType to CWE identifier string, or NULL if unmapped */
const char *error_to_cwe(ErrorType type);

/*
 * get_source_line - Read a specific line from source file
 */
int get_source_line(const char *source_file, int line_number,
                    char *buffer, size_t buffer_size);

/* Check if a file exists at the given path */
bool file_exists(const char *path);

bool is_source_file(const char *path);

bool is_cpp_file(const char *path);

bool is_directory(const char *path);

/*
 * find_compile_commands - Walk up from start_dir looking for compile_commands.json
 *                         or build/compile_commands.json. Returns 0 if found.
 */
int find_compile_commands(const char *start_dir, char *out_path, size_t out_size);

/*
 * get_project_sources - Extract all source file paths from compile_commands.json
 */
int get_project_sources(const char *json_path, char (*sources)[MAX_PATH_LEN], int max_sources);

/*
 * clean_compile_flags - Strip compiler name, -c, -o, source/object files
 *                       from a raw compile_commands.json command string
 */
void clean_compile_flags(const char *raw_flags, char *cleaned, size_t cleaned_size);

/*
 * compile_project_binary - Compile each source with its own flags from
 *                           compile_commands.json, then link all .o files
 *                           into the final binary.
 */
int compile_project_binary(const char *json_path, const char **sources,
                            int source_count, const char *extra_flags,
                            const char *link_flags,
                            const char *binary,
                            char *output, size_t output_size);

/*
 * auto_detect_link_flags - Look for a pre-built binary in the project's build
 *                           directory and extract library link flags via ldd.
 *                           Returns 0 if flags were found.
 */
int auto_detect_link_flags(const char *json_path, const char **sources,
                            int source_count,
                            char *flags_buf, size_t flags_size);

/*
 * has_main_function - Check if source file contains main()
 */
bool has_main_function(const char *source_file);

/*
 * generate_temp_path - Create a temporary file path
 */
char *generate_temp_path(const char *prefix, char *buffer, size_t buffer_size);

int cleanup_binary(const char *binary);

/* Check if haystack contains needle (case-sensitive) */
bool string_contains(const char *haystack, const char *needle);

/* Check if str starts with prefix */
bool string_starts_with(const char *str, const char *prefix);

/* Remove leading and trailing whitespace */
void trim_whitespace(char *str);

/* Get file size in bytes, or -1 on error */
long get_file_size(const char *path);

/*
 * run_max_analysis - Run ALL analysis modes and aggregate results
 */
int run_max_analysis(const char **sources, int source_count,
                     DetectedError *errors, int max_errors,
                     TestResult *result, int timeout_sec,
                     const ColorCodes *colors);

/*
 * generate_sarif_report - Write SARIF v2.1.0 JSON report
 */
int generate_sarif_report(const char *sarif_path,
                           const DetectedError *errors, int error_count,
                           long execution_time_ms);

/*
 * generate_ci_output - Write CI-friendly JSON summary to stdout
 */
void generate_ci_output(const char **source_files, int source_count,
                         const DetectedError *errors, int error_count,
                         int exit_status, long execution_time_ms);

/*
 * generate_html_report - Write HTML report with error details
 */
int generate_html_report(const char *html_path, const char **source_files,
                          int source_count,
                          const TestResult *result,
                          const DetectedError *errors, int error_count);

/*
 * run_fuzz_analysis - Run binary with boundary inputs to catch edge-case bugs
 */
int run_fuzz_analysis(const char *binary,
                      DetectedError *errors, int max_errors,
                      char *sanitizer_output, size_t sanitizer_size,
                      int timeout_sec);

/*
 * run_with_rerun - Run binary multiple times to detect non-deterministic bugs
 */
int run_with_rerun(const char *binary, int rerun_count,
                   DetectedError *errors, int max_errors,
                   char *output, size_t output_size,
                   char *error_output, size_t error_size,
                   int timeout_sec);

/*
 * check_resource_leaks - Detect file descriptor and OS resource leaks
 */
int check_resource_leaks(const char *binary,
                         DetectedError *errors, int max_errors,
                         char *sanitizer_output, size_t sanitizer_size,
                         int timeout_sec);

/*
 * check_binary_harden - Check ELF binary for security hardening features
 */
int check_binary_harden(const char *binary,
                         DetectedError *errors, int max_errors);

/*
 * scan_dangerous_apis - Scan source for dangerous C functions
 */
int scan_dangerous_apis(const char **sources, int source_count,
                        DetectedError *errors, int max_errors);

/*
 * get_execution_time - Calculate elapsed time in milliseconds
 */
void get_execution_time(struct timespec *start, struct timespec *end, long *ms);

/*
 * compile_with_basic_flags - Quick compile with -Wall -Wextra -Werror only
 */
int compile_with_basic_flags(const char **sources, int source_count,
                              const char *binary,
                              char *output, size_t output_size);

/*
 * run_coverage_analysis - Compile and run with --gcov code coverage
 */
int run_coverage_analysis(const char *binary, const char **sources,
                           int source_count, DetectedError *errors,
                           int max_errors, char *output, size_t output_size,
                           int timeout_sec);

/*
 * run_libfuzzer_analysis - Compile and run with libFuzzer
 */
int run_libfuzzer_analysis(const char **sources, int source_count,
                            DetectedError *errors, int max_errors,
                            char *output, size_t output_size,
                            int timeout_sec);

/*
 * run_ultra_analysis - Run ALL analysis modes in parallel
 */
int run_ultra_analysis(const char **sources, int source_count,
                        DetectedError *errors, int max_errors,
                        int timeout_sec, const ColorCodes *colors);

/*
 * save_baseline - Save current errors as a baseline JSON file
 */
int save_baseline(const char *path, const DetectedError *errors,
                   int error_count);

/*
 * load_baseline - Load a baseline and filter matching errors
 */
int load_baseline_and_filter(const char *path,
                              DetectedError *errors, int *error_count);

/*
 * compute_source_hash - Compute MD5 hash of source file contents
 */
int compute_source_hash(const char *source_file, char *hash_buf,
                         size_t hash_buf_size);

/*
 * save_cache_entry - Save compilation result to cache
 */
void save_cache_entry(const char *hash, bool success,
                       const char *compiler_output);

/*
 * load_cache_entry - Load cached compilation result
 */
int load_cache_entry(const char *hash, bool *success,
                      char *compiler_output, size_t output_size);

#endif /* PRISM_H */
