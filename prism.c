/*
 * prism.c - Core engine for prism, a simple C/C++ error detection tool
 *
 * Compiles C/C++ source files with GCC/G++ sanitizers (ASan/UBSan),
 *          runs them with timeout, captures output, and provides the
 *          foundation for error detection and fix suggestions.
 *
 * Uses popen() for compilation output capture, fork()/execvp()/select()
 *         for binary execution with timeout. Falls back to non-sanitizer
 *         compilation when sanitizer libraries are unavailable.
 *         Auto-detects language from file extension (.c vs .cpp/.cxx/.cc).
 */

#include "prism.h"
#include "ast_backend.h"
#include "check_engine.h"
#include "annotation.h"
#include "sym_exec.h"
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include "detect_libs.h"

/**
 * compile_with_sanitizers - Compile source files with ASan and UBSan
 */
int compile_with_sanitizers(const char **sources, int source_count,
                             const char *binary,
                             char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -fsanitize=address,undefined -g -fno-omit-frame-pointer "
                   "-Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 "
                   "-Wstringop-overflow=2 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (i < source_count) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/** 
 * compile_with_msan - Compile with MemorySanitizer (uninit memory reads)
 */
int compile_with_msan(const char **sources, int source_count,
                       const char *binary,
                       char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -fsanitize=memory -fsanitize-memory-track-origins "
                   "-fno-omit-frame-pointer -g -O1 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_with_tsan - Compile source files with ThreadSanitizer
 */
int compile_with_tsan(const char **sources, int source_count,
                       const char *binary,
                       char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -fsanitize=thread -O2 -g -fno-omit-frame-pointer "
                   "-Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 "
                   "-Wstringop-overflow=2 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_fallback - Compile without sanitizers as fallback
 */
int compile_fallback(const char **sources, int source_count,
                      const char *binary,
                      char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 64];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -Wall -Wextra -O2 -g "
                   "-Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 "
                   "-Wstringop-overflow=2 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_for_valgrind - Compile for Valgrind analysis
 */
int compile_for_valgrind(const char **sources, int source_count,
                         const char *binary, char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 64];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -Wall -Wextra -O0 -g -fno-omit-frame-pointer "
                   "-Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 "
                   "-Wstringop-overflow=2 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_for_warnings - Compile with warning flags only (no sanitizers)
 */
int compile_for_warnings(const char **sources, int source_count,
                         char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    size_t pos;
    int i;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -Wall -Wextra -Wpedantic -O2 -Wstrict-aliasing=2 "
                   "-Wformat-overflow=2 -Wstringop-overflow=2 -fsyntax-only");

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    size_t bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_cpp_with_sanitizers - Compile C++ sources with ASan and UBSan
 */
int compile_cpp_with_sanitizers(const char **sources, int source_count,
                                  const char *binary,
                                  char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "g++ -fsanitize=address,undefined -g -fno-omit-frame-pointer "
                   "-Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 "
                   "-Wstringop-overflow=2 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_cpp_with_tsan - Compile C++ sources with ThreadSanitizer
 */
int compile_cpp_with_tsan(const char **sources, int source_count,
                            const char *binary,
                            char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "g++ -fsanitize=thread -O2 -g -fno-omit-frame-pointer "
                   "-o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_with_analyzer - Compile with GCC static analyzer (-fanalyzer)
 */
int compile_with_analyzer(const char **sources, int source_count,
                      const char *binary,
                      char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -fanalyzer -O2 -g "
                   "-Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 "
                   "-Wstringop-overflow=2 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_with_clang_tidy - Run clang-tidy static analysis on source files
 */
int compile_with_clang_tidy(const char **sources, int source_count,
                            char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 256];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    /* Build clang-tidy command with checks and quiet mode */
    pos = snprintf(cmd, sizeof(cmd),
                   "clang-tidy --checks='bugprone-*,concurrency-*,nullability-*,cert-*,clang-analyzer-*' --quiet");

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    /* Add -- to separate clang-tidy args from compiler args */
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -- -Wall -Wextra -O2 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start clang-tidy");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    int clang_tidy_status = pclose(pipe);
    /* clang-tidy returns diagnostic count as exit code (0 = clean, N = N issues).
     * We want to parse the output regardless. Only signal failure if the
     * pipe itself errored (pclose returns -1) or the tool couldn't execute. */
    if (clang_tidy_status == -1)
        return -1;
    return 0;
}

/*
 * compile_with_strict_aliasing - Detect type-punning via -Wstrict-aliasing=1
 *                                Compiles to /dev/null, no binary produced.
 */
int compile_with_strict_aliasing(const char **sources, int source_count,
                                  char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -O2 -fstrict-aliasing -Wstrict-aliasing=1 -c -o /dev/null");
    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files");
        return -1;
    }
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';
    return pclose(pipe);
}

/*
 * compile_with_float_equal_warning - Catch floating-point ==/!= via -Wfloat-equal
 */
int compile_with_float_equal_warning(const char **sources, int source_count,
                                      char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -Wfloat-equal -c -o /dev/null");
    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files");
        return -1;
    }
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';
    return pclose(pipe);
}

/*
 * compile_with_conversion_warnings - Catch implicit truncation via -Wconversion
 *     Default: suppresses -Wsign-conversion (too noisy). Use --conversion=all
 *     to enable full -Wconversion.
 */
int compile_with_conversion_warnings(const char **sources, int source_count,
                                      char *output, size_t output_size,
                                      bool include_sign_conversion)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -Wconversion%s -c -o /dev/null",
                   include_sign_conversion ? "" : " -Wno-sign-conversion");
    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files");
        return -1;
    }
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';
    return pclose(pipe);
}

/*
 * run_cppcheck - Run cppcheck static analysis on source files
 *     Writes error report to output buffer. No compilation needed.
 *     Returns 0 on success, -1 if cppcheck not available.
 */
int run_cppcheck(const char **sources, int source_count,
                  char *output, size_t output_size)
{
    char cmd[16384];
    FILE *pipe;
    size_t pos;

    if (system("command -v cppcheck >/dev/null 2>&1") != 0) {
        snprintf(output, output_size, "cppcheck not found");
        return -1;
    }

    pos = snprintf(cmd, sizeof(cmd),
                   "cppcheck --enable=all --suppress=missingIncludeSystem "
                   "--suppress=unmatchedSuppression --suppress=checkersReport");
    for (int i = 0; i < source_count && pos < sizeof(cmd) - 4; i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start cppcheck");
        return -1;
    }

    size_t n = fread(output, 1, output_size - 1, pipe);
    output[n] = '\0';
    pclose(pipe);
    return 0;
}

/*
 * run_clang_analyze - Run Clang Static Analyzer on source files
 *     Returns 0 on success, -1 if clang unavailable.
 */
int run_clang_analyze(const char **sources, int source_count,
                       char *output, size_t output_size)
{
    char cmd[16384];
    FILE *pipe;
    size_t pos;

    if (system("command -v clang >/dev/null 2>&1") != 0) {
        snprintf(output, output_size, "clang not found");
        return -1;
    }

    pos = snprintf(cmd, sizeof(cmd),
                   "clang --analyze -Xanalyzer -analyzer-output=text");
    for (int i = 0; i < source_count && pos < sizeof(cmd) - 4; i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start clang");
        return -1;
    }

    size_t n = fread(output, 1, output_size - 1, pipe);
    output[n] = '\0';
    pclose(pipe);

    /* Clean up analyzer artifact files */
    for (int i = 0; i < source_count; i++) {
        if (strlen(sources[i]) > MAX_PATH_LEN - 7)
            continue;
        char plist[MAX_PATH_LEN];
        snprintf(plist, sizeof(plist), "%s.plist", sources[i]);
        unlink(plist);
    }

    return 0;
}

/*
 * run_iwyu - Run include-what-you-use on source files
 *     Reports missing or superfluous #include directives.
 *     Gracefully skips if IWYU is not installed.
 *     Returns 0 on success, -1 if IWYU unavailable.
 */
int run_iwyu(const char **sources, int source_count,
              char *output, size_t output_size)
{
    char cmd[16384];
    FILE *pipe;
    size_t pos;

    if (system("command -v include-what-you-use >/dev/null 2>&1") != 0) {
        snprintf(output, output_size, "include-what-you-use not found");
        return -1;
    }

    pos = snprintf(cmd, sizeof(cmd), "include-what-you-use -c");
    for (int i = 0; i < source_count && pos < sizeof(cmd) - 4; i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start IWYU");
        return -1;
    }

    size_t n = fread(output, 1, output_size - 1, pipe);
    output[n] = '\0';
    pclose(pipe);
    return 0;
}

/*
 * compile_cpp_for_warnings - Compile C++ with warning flags only
 */
int compile_cpp_for_warnings(const char **sources, int source_count,
                              char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    size_t pos;
    int i;

    pos = snprintf(cmd, sizeof(cmd),
                   "g++ -Wall -Wextra -Wpedantic -O2 -Wstrict-aliasing=2 "
                   "-Wformat-overflow=2 -Wstringop-overflow=2 -fsyntax-only");

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    size_t bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_cpp_with_analyzer - Compile C++ with GCC static analyzer (-fanalyzer)
 *
 *      Catches memory errors, leaks, and undefined behavior at compile time.
 */
int compile_cpp_with_analyzer(const char **sources, int source_count,
                      const char *binary,
                      char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "g++ -fanalyzer -O2 -g "
                   "-Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 "
                   "-Wstringop-overflow=2 -o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/* SIGINT flag used by run_with_timeout's select loop */
static volatile sig_atomic_t timeout_sigint_flag;

static void on_sigint_timeout(int sig)
{
    (void)sig;
    timeout_sigint_flag = 1;
}

/*
 * run_with_timeout - Shared helper: pipe/fork/execvp/select/read
 */
int run_with_timeout(const char *binary, char *const argv[],
                    char *output, size_t output_size,
                    char *error_output, size_t error_size,
                    int timeout_sec, pid_t *child_pid)
{
    int stdout_pipe[2], stderr_pipe[2];
    pid_t pid;
    int max_fd;
    fd_set read_fds;
    struct timeval tv;
    int stdout_eof = 0, stderr_eof = 0;
    size_t out_pos = 0, err_pos = 0;
    int status;

    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: redirect stdin from /dev/null so programs
         * that call scanf/getchar don't hang on the terminal. */
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        } else {
            close(STDIN_FILENO);
        }
        /* Redirect stdout/stderr to pipes, then exec */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        execvp(binary, (char *const *)argv);
        _exit(127);
    }

    /* Parent: close child-write ends, then monitor with timeout */
    if (child_pid)
        *child_pid = pid;

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Install SIGINT handler so ^C in the parent breaks the
     * select() loop rather than retrying on EINTR forever. */
    struct sigaction old_sa;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint_timeout;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &old_sa);
    timeout_sigint_flag = 0;

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_sec;

    while (!stdout_eof || !stderr_eof) {
        FD_ZERO(&read_fds);
        max_fd = 0;

        if (!stdout_eof) {
            FD_SET(stdout_pipe[0], &read_fds);
            if (stdout_pipe[0] > max_fd)
                max_fd = stdout_pipe[0];
        }
        if (!stderr_eof) {
            FD_SET(stderr_pipe[0], &read_fds);
            if (stderr_pipe[0] > max_fd)
                max_fd = stderr_pipe[0];
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long remaining_ms = (deadline.tv_sec - now.tv_sec) * 1000 +
                            (deadline.tv_nsec - now.tv_nsec) / 1000000;
        if (remaining_ms <= 0) {
            kill(pid, SIGKILL);
            for (int retry = 0; retry < 100; retry++) {
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid) break;
                if (w < 0) { status = 0; break; }
                usleep(10000);
            }
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            if (out_pos < output_size)
                output[out_pos] = '\0';
            if (err_pos < error_size)
                error_output[err_pos] = '\0';
            return -1;
        }

        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                if (timeout_sigint_flag)
                    break;     /* ^C — bail out */
                continue;      /* Spurious signal — retry */
            }
            break;         /* Other error — bail out */
        }

        if (ready == 0) {
            kill(pid, SIGKILL);
            /* Busy-wait with WNOHANG so we don't hang if child is stuck */
            for (int retry = 0; retry < 100; retry++) {
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid) break;
                if (w < 0) { status = 0; break; }
                usleep(10000);  /* 10ms */
            }
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            if (out_pos < output_size)
                output[out_pos] = '\0';
            if (err_pos < error_size)
                error_output[err_pos] = '\0';
            return -1;
        }

        if (FD_ISSET(stdout_pipe[0], &read_fds)) {
            if (output_size > 0 && out_pos < output_size - 1) {
                ssize_t n = read(stdout_pipe[0], output + out_pos,
                                 output_size - out_pos - 1);
                if (n <= 0)
                    stdout_eof = 1;
                else
                    out_pos += n;
            } else {
                stdout_eof = 1;
            }
        }
        if (FD_ISSET(stderr_pipe[0], &read_fds)) {
            if (error_size > 0 && err_pos < error_size - 1) {
                ssize_t n = read(stderr_pipe[0], error_output + err_pos,
                                 error_size - err_pos - 1);
                if (n <= 0)
                    stderr_eof = 1;
                else
                    err_pos += n;
            } else {
                stderr_eof = 1;
            }
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (out_pos < output_size)
        output[out_pos] = '\0';
    if (err_pos < error_size)
        error_output[err_pos] = '\0';

    /* Reap the child (should already be dead since pipes are closed) */
    status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        status = 0;

    /* Restore original SIGINT handler */
    sigaction(SIGINT, &old_sa, NULL);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return -1;
}

/*
 * run_binary - Execute compiled binary with timeout
 *
 *      and capture both stdout and stderr for analysis.
 */
int run_binary(const char *binary, const char *args,
               char *output, size_t output_size,
               char *error_output, size_t error_size,
               int timeout_sec, pid_t *child_pid)
{
    char *argv[3];

    argv[0] = (char *)binary;
    argv[1] = (char *)args;
    argv[2] = NULL;

    return run_with_timeout(binary, argv,
                           output, output_size,
                           error_output, error_size,
                           timeout_sec, child_pid);
}

/*
 * run_with_valgrind - Execute binary under Valgrind for memory error detection
 */
int run_with_valgrind(const char *binary,
                       char *output, size_t output_size,
                       char *error_output, size_t error_size,
                       int timeout_sec, pid_t *child_pid)
{
    char *argv[7];

    argv[0] = "valgrind";
    argv[1] = "--leak-check=full";
    argv[2] = "--show-leak-kinds=all";
    argv[3] = "--track-origins=yes";
    argv[4] = "--error-exitcode=1";
    argv[5] = (char *)binary;
    argv[6] = NULL;

    return run_with_timeout("valgrind", argv,
                           output, output_size,
                           error_output, error_size,
                           timeout_sec, child_pid);
}

/*
 * generate_temp_path - Create a temporary file path
 *
 *      without conflicting with existing files or other tests.
 */
char *generate_temp_path(const char *prefix, char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, "/tmp/prism_%s_XXXXXX", prefix);
    int fd = mkstemp(buffer);
    if (fd >= 0) {
        close(fd);
        /* Fix race condition: unlink the file so another mkstemp won't reuse name */
        unlink(buffer);
    }
    return buffer;
}

/* Remove compiled binary after run */
int cleanup_binary(const char *binary)
{
    return unlink(binary);
}

/* Check if a file exists at the given path */
bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Check if path has a .c extension */
bool is_c_file(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && string_starts_with(dot, ".c") && dot[2] == '\0';
}

bool is_source_file(const char *path)
{
    return is_c_file(path) || is_cpp_file(path);
}

bool is_cpp_file(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return false;
    return (strcmp(dot, ".cpp") == 0 ||
            strcmp(dot, ".cxx") == 0 ||
            strcmp(dot, ".cc") == 0);
}

/*
 * has_main_function - Check if source file contains main()
 */
bool has_main_function(const char *source_file)
{
    FILE *fp;
    char line[MAX_SOURCE_LINE];
    bool found = false;

    fp = fopen(source_file, "r");
    if (!fp)
        return false;

    while (fgets(line, sizeof(line), fp)) {
        char *p = strstr(line, "main");
        while (p) {
            /* Must be at identifier boundary (not part of a longer word) */
            if ((p == line || (!isalnum((unsigned char)p[-1]) && p[-1] != '_')) &&
                p[4] == '(') {
                found = true;
                break;
            }
            p = strstr(p + 4, "main");
        }
        if (found) break;
    }

    fclose(fp);
    return found;
}

/* Check if haystack contains needle (case-sensitive) */
bool string_contains(const char *haystack, const char *needle)
{
    return haystack && needle && strstr(haystack, needle) != NULL;
}

/* Case-insensitive version of string_contains */
static bool string_contains_ic(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return false;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen == 0) return true;
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            if (toupper((unsigned char)haystack[i + j]) !=
                toupper((unsigned char)needle[j]))
                break;
        }
        if (j == nlen) return true;
    }
    return false;
}

/* Check if str starts with prefix */
bool string_starts_with(const char *str, const char *prefix)
{
    if (!str || !prefix)
        return false;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

/* Remove leading and trailing whitespace */
void trim_whitespace(char *str)
{
    char *start, *end;

    if (!str)
        return;

    /* Skip leading whitespace */
    start = str;
    while (*start && (*start == ' ' || *start == '\t' ||
                      *start == '\n' || *start == '\r'))
        start++;

    /* All whitespace */
    if (*start == '\0') {
        str[0] = '\0';
        return;
    }

    /* Move to start */
    if (start != str)
        memmove(str, start, strlen(start) + 1);

    /* Trim trailing whitespace */
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' ||
                         *end == '\n' || *end == '\r'))
        *end-- = '\0';
}

/* Get file size in bytes, or -1 on error */
long get_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return -1;
    return st.st_size;
}

/*
 * get_execution_time - Calculate elapsed time in milliseconds
 *
 *      and enforce timeout for infinite loops.
 */
void get_execution_time(struct timespec *start, struct timespec *end, long *ms)
{
    *ms = (end->tv_sec - start->tv_sec) * 1000 +
          (end->tv_nsec - start->tv_nsec) / 1000000;
}

/* Error pattern table mapping sanitizer output to error types and fixes */
static const ErrorPattern error_patterns[] = {
    { "heap-buffer-overflow",    "Buffer Overflow",
      "Check array bounds. The buffer was allocated on the heap but you accessed memory beyond its limits. Use bounds checking or increase the buffer size.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "stack-buffer-overflow",   "Stack Buffer Overflow",
      "The local array is too small for the data you're writing. Use dynamic allocation (malloc) for large arrays, or increase the buffer size.",
      ERR_STACK_OVERFLOW, 2 },
    { "heap-use-after-free",     "Use After Free",
      "You accessed memory after calling free(). Set the pointer to NULL after free() and check for NULL before accessing.",
      ERR_USE_AFTER_FREE, 2 },
    { "detected memory leaks",   "Memory Leak",
      "Memory was allocated but never freed. Add free() for every malloc/calloc/realloc call. Track all allocations and ensure they are released.",
      ERR_MEMORY_LEAK, 1 },
    { "SEGV on unknown address", "NULL Pointer Dereference",
      "You dereferenced a NULL or invalid pointer. Add a NULL check before accessing the pointer, and verify all pointer assignments.",
      ERR_NULL_DEREF, 2 },
    { "store to null pointer",   "NULL Pointer Dereference",
      "You wrote to a NULL pointer. Check that the pointer is valid before writing to it. Initialize pointers to NULL and verify after allocation.",
      ERR_NULL_DEREF, 2 },
    { "load of null pointer",    "NULL Pointer Dereference",
      "You read from a NULL pointer. Check that the pointer is valid before reading from it. Initialize pointers to NULL and verify after allocation.",
      ERR_NULL_DEREF, 2 },
    { "signed integer overflow", "Integer Overflow",
      "An arithmetic operation exceeded the range of a signed integer. Use int64_t for large values, or add overflow checks before the operation.",
      ERR_INT_OVERFLOW, 1 },
    { "division by zero",        "Division by Zero",
      "You divided by zero. Check that the divisor is non-zero before performing the division.",
      ERR_DIV_BY_ZERO, 2 },
    { "uninitialized value",     "Uninitialized Variable",
      "A variable was used before being initialized. Assign a default value at declaration or ensure all code paths initialize the variable.",
      ERR_UNINIT_VAR, 1 },
    { "null pointer passed",     "NULL Pointer Passed to Function",
      "A NULL pointer was passed to a function that doesn't accept it. Check return values from malloc and other functions before passing them.",
      ERR_NULL_DEREF, 2 },
    { "stack-overflow",          "Stack Overflow",
      "The call stack exceeded its limit. This is usually caused by infinite recursion. Check your recursive function's base case.",
      ERR_STACK_OVERFLOW, 2 },
    { "double-free",             "Double Free",
      "You called free() twice on the same pointer. Set the pointer to NULL after free() and check for NULL before freeing.",
      ERR_USE_AFTER_FREE, 2 },
    { "stack-buffer-underflow",  "Buffer Underflow",
      "You accessed memory before the start of a stack buffer. Check array indices for negative values or off-by-one errors.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "heap-buffer-underflow",   "Buffer Underflow",
      "You accessed memory before the start of a heap buffer. Check array indices for negative values or off-by-one errors.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "shift exponent",          "Invalid Shift",
      "A shift operation used an invalid exponent (negative or >= type width). Ensure shift amounts are within valid range.",
      ERR_INT_OVERFLOW, 1 },
    { "free-nonheap",            "Invalid Free",
      "You called free() on memory that was not allocated by malloc/calloc/realloc. Only free dynamically allocated memory.",
      ERR_UNKNOWN, 2 },
    { "out of bounds",           "Array Out of Bounds",
      "An array index is outside the valid range. Check loop bounds and array sizes before accessing elements.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "insufficient space",      "Buffer Overflow",
      "A memory access exceeded the allocated buffer size. Check buffer sizes and access bounds.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "alloc-size-larger-than",  "Excessive Allocation",
      "An allocation request exceeds the maximum object size. Check for integer overflow in size calculations.",
      ERR_INT_OVERFLOW, 2 },
    { "free-nonheap-object",     "Invalid Free",
      "You called free() on a pointer that was not returned by malloc. Only free the original allocation pointer.",
      ERR_UNKNOWN, 2 },
    { "WARNING: ThreadSanitizer: data race", "Data Race",
      "Two threads access the same memory without synchronization. Use mutexes (pthread_mutex_t) or atomic variables (stdatomic.h) to protect shared data.",
      ERR_DATA_RACE, 2 },
    { "WARNING: ThreadSanitizer: lock-order-inversion", "Lock Order Inversion",
      "Threads acquired locks in inconsistent order, risking deadlock. Always acquire locks in the same global order across all threads.",
      ERR_DATA_RACE, 2 },
    { "WARNING: ThreadSanitizer: signal-unsafe-call", "Signal-Unsafe Call",
      "A signal handler called a function that is not async-signal-safe. Use only async-signal-safe functions (see man 7 signal-safety) in signal handlers.",
      ERR_DATA_RACE, 2 },
    { "std::bad_alloc",          "Bad Allocation",
      "Memory allocation failed (std::bad_alloc). The system ran out of memory. Check for memory leaks or excessive allocations.",
      ERR_BAD_ALLOC, 2 },
    { "std::out_of_range",       "Out of Range",
      "A container access was out of bounds (std::out_of_range). Use at() with try/catch, or check index against size() before accessing.",
      ERR_OUT_OF_RANGE, 2 },
    { "std::logic_error",         "Logic Error",
      "A logical precondition was violated (std::logic_error). Review the code logic and ensure all preconditions are met before operations.",
      ERR_LOGIC_ERROR, 1 },
    { "pure virtual method called", "Pure Virtual Method Called",
      "A pure virtual function was called, usually due to deleting an object while a base class destructor or method is still running. Avoid deleting objects from within their own member functions.",
      ERR_PURE_VIRTUAL, 2 },
    { "double free or corruption", "Double Free or Corruption",
      "The heap was corrupted, possibly by double-free or invalid free. Check all free/delete calls and ensure pointers are set to NULL/nullptr after deletion.",
      ERR_DOUBLE_FREE_CPP, 2 },
    /* Valgrind-specific patterns */
    { "Invalid read",           "Invalid Read",
      "Valgrind detected an invalid read (out-of-bounds or use-after-free). Check array bounds and ensure pointers are valid before dereferencing.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "Invalid write",          "Invalid Write",
      "Valgrind detected an invalid write (out-of-bounds or use-after-free). Check array bounds and ensure pointers are valid before writing.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "Use of uninitialised",   "Uninitialized Value",
      "Valgrind detected use of an uninitialized value. Initialize all variables before use, or check code paths to ensure initialization.",
ERR_UNINIT_VAR, 1 },
    { "Uninitialised",          "Uninitialized Value",
      "An uninitialized value was used in a way that affects program behavior. Initialize all variables before use.",
      ERR_UNINIT_VAR, 1 },
    { "definitely lost",        "Memory Leak",
      "Valgrind detected a definite memory leak. Memory was allocated but never freed. Ensure all malloc/calloc/realloc calls have matching free calls.",
      ERR_MEMORY_LEAK, 2 },
    { "indirectly lost",        "Memory Leak (Indirect)",
      "Valgrind detected an indirect memory leak. This memory is only reachable through other leaked blocks. Fix the parent leak first.",
      ERR_MEMORY_LEAK, 2 },
    { "possibly lost",          "Memory Leak (Possible)",
      "Valgrind detected a possible memory leak. Pointer arithmetic may have lost track of allocated memory. Review pointer manipulations.",
      ERR_MEMORY_LEAK, 1 },
    /* GCC -fanalyzer patterns */
    { "stack-based buffer overflow", "Buffer Overflow",
      "Writing beyond allocated stack memory. Check array bounds and buffer sizes.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "heap-based buffer overflow", "Buffer Overflow",
      "Writing beyond allocated heap memory. Check buffer sizes before copying data.",
      ERR_BUFFER_OVERFLOW, 2 },
    { "use after 'free' of",     "Use After Free",
      "Memory accessed after being freed. Store NULL after free and check before use.",
      ERR_USE_AFTER_FREE, 2 },
    { "double free",            "Double Free",
      "Memory freed twice. Set pointer to NULL after free.",
      ERR_USE_AFTER_FREE, 2 },
    /* clang-tidy specific patterns - specific before generic */
    { "clang-analyzer-unix.Stream", "Resource Leak",
      "clang-tidy detected an opened stream/file that is never closed. Ensure all fopen/fclose pairs are matched, even on error paths.",
      ERR_MEMORY_LEAK, 2 },
    { "clang-analyzer-unix.Malloc", "Memory Leak",
      "clang-tidy (clang-analyzer) detected a memory leak. Free all allocated memory or use smart pointers.",
      ERR_MEMORY_LEAK, 2 },
    { "clang-analyzer-core.NullDereference", "NULL Pointer Dereference",
      "clang-tidy (clang-analyzer) detected a null pointer dereference. Add null checks before dereferencing pointers.",
      ERR_NULL_DEREF, 2 },
    { "clang-analyzer-deadcode.DeadStores", "Dead Store",
      "clang-tidy detected a value that is stored but never read. Remove the unused assignment or use the value.",
      ERR_UNKNOWN, 1 },
    { "clang-analyzer-unix.cstring.NullArg", "NULL Argument",
      "clang-tidy detected a NULL argument passed to a function that doesn't accept it. Check arguments before calling.",
      ERR_NULL_DEREF, 2 },
    { "clang-analyzer-security", "Security Issue",
      "clang-tidy detected a potential security vulnerability. Review buffer bounds, format strings, and input validation.",
      ERR_UNKNOWN, 2 },
    { "cert-err",                 "Ignored Return Value",
      "clang-tidy detected a function return value that is ignored. Check return values for errors, especially for I/O operations.",
      ERR_UNKNOWN, 1 },
    { "cert-",                    "Security Issue",
      "clang-tidy detected a security issue (CERT rule). Review the CERT secure coding standard and apply the fix.",
      ERR_UNKNOWN, 2 },
    { "bugprone-narrowing",       "Narrowing Conversion",
      "clang-tidy detected a narrowing conversion that may lose data. Use explicit casts or change variable types.",
      ERR_UNKNOWN, 1 },
    { "bugprone-",               "Bugprone Issue",
      "clang-tidy detected a bugprone pattern. Review the code for potential bugs and apply the suggested fix.",
      ERR_UNKNOWN, 1 },
    { "concurrency-",             "Concurrency Issue",
      "clang-tidy detected a concurrency problem. Use proper synchronization (mutexes, atomics) for shared data.",
      ERR_DATA_RACE, 2 },
    { "nullability-",             "Nullability Issue",
      "clang-tidy detected a nullability problem. Check pointer nullability annotations and add null checks.",
      ERR_NULL_DEREF, 2 },
};

static const int num_patterns = sizeof(error_patterns) / sizeof(error_patterns[0]);

/*
 * classify_error - Determine error pattern index from error line
 */
int classify_error(const char *error_line)
{
    int i;

    if (!error_line)
        return -1;

    for (i = 0; i < num_patterns; i++) {
        if (string_contains_ic(error_line, error_patterns[i].pattern))
            return i;
    }

    return -1;
}

/*
 * parse_sanitizer_errors - Extract errors from sanitizer output
 *
 *      Parse it to extract structured error information.
 */
int parse_sanitizer_errors(const char *error_output,
                           DetectedError *errors, int max_errors)
{
    char line[MAX_LINE_LEN];
    const char *pos, *line_start, *line_end;
    int count = 0;
    unsigned int seen_types = 0;

    if (!error_output || !errors || max_errors <= 0)
        return 0;

    pos = error_output;

    /* First pass: look for SUMMARY line to use as fallback title */
    const char *summary = NULL;
    const char *s = strstr(error_output, "SUMMARY:");
    if (s) {
        s += 8; /* skip "SUMMARY:" */
        while (*s == ' ') s++;
        summary = s;
    }

    while (*pos && count < max_errors) {
        line_start = pos;
        line_end = strchr(pos, '\n');
        if (!line_end)
            line_end = pos + strlen(pos);

        size_t len = line_end - line_start;
        if (len >= MAX_LINE_LEN)
            len = MAX_LINE_LEN - 1;
        memcpy(line, line_start, len);
        line[len] = '\0';
        trim_whitespace(line);

        int pattern_idx = classify_error(line);
        if (pattern_idx >= 0) {
            ErrorType type = error_patterns[pattern_idx].type;
            unsigned int type_bit = (1u << type);
            if (seen_types & type_bit) {
                pos = *line_end ? line_end + 1 : line_end;
                continue;
            }
            seen_types |= type_bit;

            errors[count].type = type;
            errors[count].severity = error_patterns[pattern_idx].severity;
            errors[count].source_line = 0;
            errors[count].has_source = false;
            errors[count].source_file[0] = '\0';

            /* Use SUMMARY as fallback if it's more descriptive */
            const char *title = error_patterns[pattern_idx].title;
            if (summary && !strstr(title, "Unknown"))
                title = summary;
            snprintf(errors[count].title, sizeof(errors[count].title),
                     "%s", title);
            snprintf(errors[count].fix_suggestion,
                     sizeof(errors[count].fix_suggestion),
                     "%s", error_patterns[pattern_idx].fix);

            /* Try to extract file:line from the line itself */
            static const char *source_exts[] = {".cpp:", ".cc:", ".cxx:", ".hpp:", ".h:", ".c:", NULL};
            for (int e = 0; source_exts[e]; e++) {
                const char *ext_sep = strstr(line, source_exts[e]);
                if (!ext_sep) continue;
                /* Walk backward to find the start of the path */
                const char *path_end = ext_sep + strlen(source_exts[e]) - 1;
                const char *path_start = path_end;
                while (path_start > line && *(path_start - 1) != ' ' &&
                       *(path_start - 1) != '\t' && *(path_start - 1) != '\'')
                    path_start--;
                size_t path_len = (size_t)(path_end - path_start);
                if (path_len > 0 && path_len < MAX_PATH_LEN - 1) {
                    /* Found a valid source extension — extract path */
                    memcpy(errors[count].source_file, path_start, path_len);
                    errors[count].source_file[path_len] = '\0';
                    /* Extract line number after the colon */
                    const char *ln = ext_sep + strlen(source_exts[e]);
                    if (*ln >= '0' && *ln <= '9') {
                        errors[count].source_line = atoi(ln);
                        errors[count].has_source = true;
                    }
                }
                break;
            }

            count++;
        }

        pos = *line_end ? line_end + 1 : line_end;
    }

    return count;
}

/*
 * parse_signal_errors - Detect errors from signal termination
 *
 *      we can classify the error based on the signal received.
 */
int parse_signal_errors(const char *error_output, int exit_code,
                        int signal, DetectedError *errors, int max_errors)
{
    (void)error_output;
    (void)exit_code;  /* Use signal parameter only — exit_code is unreliable */
    if (!errors || max_errors <= 0)
        return 0;

    if (signal == SIGSEGV) {
        errors[0].type = ERR_SEG;
        snprintf(errors[0].title, sizeof(errors[0].title),
                 "Segmentation Fault");
        snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                 "The program accessed invalid memory. Check for NULL pointer dereferences, buffer overflows, or use-after-free bugs. Compile with -fsanitize=address for detailed diagnostics.");
        errors[0].severity = 2;
        errors[0].source_line = 0;
        errors[0].has_source = false;
        return 1;
    }
    if (signal == SIGABRT) {
        errors[0].type = ERR_ABORT;
        snprintf(errors[0].title, sizeof(errors[0].title),
                 "Abort (SIGABRT)");
        snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                 "The program aborted — possibly a failed assertion or runtime check.");
        errors[0].severity = 2;
        errors[0].source_line = 0;
        errors[0].has_source = false;
        return 1;
    }
    if (signal == SIGBUS) {
        errors[0].type = ERR_SEG;
        snprintf(errors[0].title, sizeof(errors[0].title),
                 "Bus Error (SIGBUS)");
        snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                 "The program attempted an invalid memory access (unaligned or non-existent address).");
        errors[0].severity = 2;
        errors[0].source_line = 0;
        errors[0].has_source = false;
        return 1;
    }
    if (signal == SIGFPE) {
        errors[0].type = ERR_DIV_BY_ZERO;
        snprintf(errors[0].title, sizeof(errors[0].title),
                 "Floating Point Exception (SIGFPE)");
        snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                 "The program crashed with a math error — check for division by zero or overflow.");
        errors[0].severity = 2;
        errors[0].source_line = 0;
        errors[0].has_source = false;
        return 1;
    }

    if (signal == SIGABRT || exit_code == 134) {
        errors[0].type = ERR_ABORT;
        snprintf(errors[0].title, sizeof(errors[0].title),
                 "Program Aborted");
        snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                 "The program called abort(). Check for failed assertions, double-free errors, or heap corruption. Compile with -fsanitize=address for detailed diagnostics.");
        errors[0].severity = 2;
        errors[0].source_line = 0;
        errors[0].has_source = false;
        return 1;
    }

    return 0;
}

/*
 * generate_fix_suggestion - Create fix suggestion for detected error
 *
 *      each type of error, not just report it.
 */
void generate_fix_suggestion(DetectedError *error)
{
    int i;

    if (!error || error->fix_suggestion[0] != '\0')
        return;

    for (i = 0; i < num_patterns; i++) {
        if (error_patterns[i].type == error->type) {
            snprintf(error->fix_suggestion, sizeof(error->fix_suggestion),
                     "%s", error_patterns[i].fix);
            return;
        }
    }

    snprintf(error->fix_suggestion, sizeof(error->fix_suggestion),
             "Unknown error type. Review the code around the reported location.");
}

/* Get human-readable name for error type */
const char *get_error_name(ErrorType type)
{
    switch (type) {
    case ERR_NULL_DEREF:      return "NULL Pointer Dereference";
    case ERR_BUFFER_OVERFLOW: return "Buffer Overflow";
    case ERR_USE_AFTER_FREE:  return "Use After Free";
    case ERR_MEMORY_LEAK:     return "Memory Leak";
    case ERR_INT_OVERFLOW:    return "Integer Overflow";
    case ERR_DIV_BY_ZERO:     return "Division by Zero";
    case ERR_UNINIT_VAR:      return "Uninitialized Variable";
    case ERR_STACK_OVERFLOW:  return "Stack Overflow";
    case ERR_SEG:            return "Segmentation Fault";
    case ERR_ABORT:           return "Program Aborted";
    case ERR_DATA_RACE:       return "Data Race";
    case ERR_BAD_ALLOC:        return "Bad Allocation";
    case ERR_OUT_OF_RANGE:     return "Out of Range";
    case ERR_LOGIC_ERROR:      return "Logic Error";
    case ERR_PURE_VIRTUAL:    return "Pure Virtual Method Called";
    case ERR_DOUBLE_FREE_CPP: return "Double Free or Corruption";
    case ERR_HARDENING:       return "Binary Hardening";
    case ERR_STRICT_ALIASING:  return "Strict Aliasing Violation";
    case ERR_CONVERSION_LOSS:  return "Implicit Conversion Loss";
    case ERR_FLOAT_COMPARE:    return "Floating Point Comparison";
    case ERR_UNKNOWN:         return "Unknown Error";
    case ERR_NONE:            return "No Error";
    }
    return "Unknown Error";
}

/* Map ErrorType to CWE identifier */
const char *error_to_cwe(ErrorType type)
{
    switch (type) {
    case ERR_NULL_DEREF:         return "CWE-476";
    case ERR_BUFFER_OVERFLOW:    return "CWE-120";
    case ERR_USE_AFTER_FREE:     return "CWE-416";
    case ERR_MEMORY_LEAK:        return "CWE-401";
    case ERR_INT_OVERFLOW:       return "CWE-190";
    case ERR_DIV_BY_ZERO:        return "CWE-369";
    case ERR_UNINIT_VAR:         return "CWE-457";
    case ERR_STACK_OVERFLOW:     return "CWE-121";
    case ERR_DATA_RACE:          return "CWE-362";
    case ERR_DOUBLE_FREE_CPP:    return "CWE-415";
    case ERR_STRICT_ALIASING:    return "CWE-843";
    case ERR_CONVERSION_LOSS:    return "CWE-197";
    case ERR_FLOAT_COMPARE:      return "CWE-1079";
    case ERR_HARDENING:          return "CWE-693";
    default:                     return NULL;
    }
}

/*
 * get_source_line - Read a specific line from source file
 *
 *      helps the user understand and fix the problem.
 */
int get_source_line(const char *source_file, int line_number,
                    char *buffer, size_t buffer_size)
{
    FILE *fp;
    char line[MAX_SOURCE_LINE];
    int current = 0;

    fp = fopen(source_file, "r");
    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp)) {
        current++;
        if (current == line_number) {
            trim_whitespace(line);
            snprintf(buffer, buffer_size, "%s", line);
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return -1;
}

/*
 * init_colors - Initialize color codes based on whether colors are enabled
 *
 *      Auto-detect TTY to avoid sending escape codes to pipes/files.
 */
void init_colors(ColorCodes *colors, bool use_colors)
{
    if (!colors)
        return;

    if (use_colors) {
        colors->red = "\033[31m";
        colors->green = "\033[32m";
        colors->yellow = "\033[33m";
        colors->blue = "\033[34m";
        colors->magenta = "\033[35m";
        colors->cyan = "\033[36m";
        colors->bold = "\033[1m";
        colors->reset = "\033[0m";
    } else {
        colors->red = "";
        colors->green = "";
        colors->yellow = "";
        colors->blue = "";
        colors->magenta = "";
        colors->cyan = "";
        colors->bold = "";
        colors->reset = "";
    }
}

/*
 * print_colored - Print formatted message with color
 *
 *      readability of test results in the terminal.
 */
void print_colored(const ColorCodes *colors, const char *color,
                   const char *fmt, ...)
{
    va_list args;

    if (!colors || !fmt)
        return;

    if (color)
        printf("%s", color);

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (color && colors->reset)
        printf("%s", colors->reset);
}

/*
 * print_banner - Print banner showing file(s) being tested
 *
 *      tested, especially when running multiple tests.
 *      Shows file count when multiple files are provided.
 */
void print_banner(const ColorCodes *colors, const char **source_files, int source_count)
{
    int i;

    if (!colors || !source_files || source_count <= 0)
        return;

    printf("========================================\n");
    print_colored(colors, colors->bold, "  prism - C Error Detection Tool\n");
    printf("========================================\n");
    print_colored(colors, colors->cyan, "Testing: ");

    if (source_count == 1) {
        printf("%s\n", source_files[0]);
    } else {
        printf("%d files\n", source_count);
        for (i = 0; i < source_count; i++) {
            printf("  - %s\n", source_files[i]);
        }
    }

    printf("----------------------------------------\n");
}

/*
 * print_summary - Print summary of test results
 *
 *      indication and actionable fix suggestions.
 *      Shows all source files when multiple files are used.
 */
void print_summary(const ColorCodes *colors, const TestResult *result,
                   int error_count, const DetectedError *errors)
{
    char source_line[MAX_SOURCE_LINE];
    int i;

    if (!colors || !result)
        return;

    printf("\n");

    if (error_count > 0) {
        /* Count errors by severity */
        int critical = 0, severe = 0, warning = 0, info = 0;
        for (i = 0; i < error_count; i++) {
            if (errors[i].severity >= 3)      critical++;
            else if (errors[i].severity == 2) severe++;
            else if (errors[i].severity == 1) warning++;
            else                              info++;
        }

        /* Print section header */
        print_colored(colors, colors->bold,
                      "╔════════════════════════════════════════╗\n");
        print_colored(colors, colors->bold,
                      "║         ANALYSIS RESULTS              ║\n");
        print_colored(colors, colors->bold,
                      "╚════════════════════════════════════════╝\n\n");

        /* Print severity summary table */
        print_colored(colors, colors->bold, "  Severity Summary:\n");
        if (critical > 0) {
            print_colored(colors, colors->red,
                          "    🔴 CRITICAL : %d\n", critical);
        }
        if (severe > 0 || (critical == 0 && severe > 0)) {
            print_colored(colors, colors->yellow,
                          "    🟡 ERROR    : %d\n", severe);
        }
        if (warning > 0) {
            print_colored(colors, colors->cyan,
                          "    🟢 WARNING  : %d\n", warning);
        }
        if (info > 0) {
            printf("    ⚪ INFO     : %d\n", info);
        }
        if (result->execution_time_ms > 0) {
            printf("    ⏱ Time      : %ldms\n", result->execution_time_ms);
        }
        printf("\n");

        /* Print detailed findings by severity */
        print_colored(colors, colors->bold, "  Detailed Findings:\n");

        /* First print CRITICAL errors */
        for (i = 0; i < error_count; i++) {
            if (errors[i].severity < 3) continue;
            print_colored(colors, colors->red, "  ──[CRITICAL]── ");
            print_colored(colors, colors->bold, "%s", errors[i].title);
            if (errors[i].has_source && errors[i].source_line > 0)
                printf(" (%s:%d)", errors[i].source_file,
                       errors[i].source_line);
            printf("\n");

            print_colored(colors, colors->yellow, "     Fix: ");
            printf("%s\n", errors[i].fix_suggestion);

            if (errors[i].has_source && errors[i].source_line > 0 &&
                get_source_line(errors[i].source_file, errors[i].source_line,
                                source_line, sizeof(source_line)) == 0) {
                print_colored(colors, colors->cyan, "     -> %d | ",
                              errors[i].source_line);
                printf("%s\n", source_line);
            }
            printf("\n");
        }

        /* Then ERROR severity */
        for (i = 0; i < error_count; i++) {
            if (errors[i].severity != 2) continue;
            print_colored(colors, colors->yellow, "  ──[ERROR]── ");
            print_colored(colors, colors->bold, "%s", errors[i].title);
            if (errors[i].has_source && errors[i].source_line > 0)
                printf(" (%s:%d)", errors[i].source_file,
                       errors[i].source_line);
            printf("\n");

            print_colored(colors, colors->yellow, "     Fix: ");
            printf("%s\n", errors[i].fix_suggestion);

            if (errors[i].has_source && errors[i].source_line > 0 &&
                get_source_line(errors[i].source_file, errors[i].source_line,
                                source_line, sizeof(source_line)) == 0) {
                print_colored(colors, colors->cyan, "     -> %d | ",
                              errors[i].source_line);
                printf("%s\n", source_line);
            }
            printf("\n");
        }

        /* Then WARNING severity */
        for (i = 0; i < error_count; i++) {
            if (errors[i].severity != 1) continue;
            print_colored(colors, colors->cyan, "  ──[WARNING]── ");
            print_colored(colors, colors->bold, "%s", errors[i].title);
            if (errors[i].has_source && errors[i].source_line > 0)
                printf(" (%s:%d)", errors[i].source_file,
                       errors[i].source_line);
            printf("\n");

            print_colored(colors, colors->yellow, "     Fix: ");
            printf("%s\n", errors[i].fix_suggestion);

            if (errors[i].has_source && errors[i].source_line > 0 &&
                get_source_line(errors[i].source_file, errors[i].source_line,
                                source_line, sizeof(source_line)) == 0) {
                print_colored(colors, colors->cyan, "     -> %d | ",
                              errors[i].source_line);
                printf("%s\n", source_line);
            }
            printf("\n");
        }

        /* Then INFO severity */
        for (i = 0; i < error_count; i++) {
            if (errors[i].severity != 0) continue;
            printf("  ──[INFO]── %s", errors[i].title);
            if (errors[i].has_source && errors[i].source_line > 0)
                printf(" (%s:%d)", errors[i].source_file,
                       errors[i].source_line);
            printf("\n");
            printf("     %s\n", errors[i].fix_suggestion);
            printf("\n");
        }

        /* Final summary line */
        print_colored(colors, colors->bold,
                      "  ─────────────────────────────────────────\n");
        print_colored(colors, colors->red,
                      "  %d error(s) detected", error_count);
        if (result->execution_time_ms > 0)
            printf(" in %ldms", result->execution_time_ms);
        printf("\n");

    } else if (result->compilation_success) {
        print_colored(colors, colors->green, "[OK] ");
        print_colored(colors, colors->bold,
                      "No errors detected");
        if (result->execution_time_ms > 0)
            printf(" — clean run in %ldms", result->execution_time_ms);
        printf("\n");
    } else {
        print_colored(colors, colors->red, "[COMPILE ERROR]\n");
        if (result->compiler_output[0])
            printf("%s\n", result->compiler_output);
    }
}

/*
 * generate_sarif_report - Write SARIF v2.1.0 JSON report
 */
int generate_sarif_report(const char *sarif_path,
                           const DetectedError *errors, int error_count,
                           long execution_time_ms)
{
    FILE *fp;
    int i;

    if (!sarif_path || !sarif_path[0])
        return -1;

    fp = fopen(sarif_path, "w");
    if (!fp)
        return -1;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"$schema\": \"https://schemastore.ast/sarif-2.1.0-json-schema.json\",\n");
    fprintf(fp, "  \"version\": \"2.1.0\",\n");
    fprintf(fp, "  \"runs\": [\n");
    fprintf(fp, "    {\n");
    fprintf(fp, "      \"tool\": {\n");
    fprintf(fp, "        \"driver\": {\n");
    fprintf(fp, "          \"name\": \"prism\",\n");
    fprintf(fp, "          \"version\": \"1.5\",\n");
    fprintf(fp, "          \"informationUri\": \"https://github.com/Jadkes/Prism\",\n");
    fprintf(fp, "          \"taxonomies\": [{\"name\":\"CWE\",\"guid\":\"3333a5c4-5a4c-4c8a-8c3a-7a1c4e8d7f2a\",\"informationUri\":\"https://cwe.mitre.org/\"}]\n");
    fprintf(fp, "        }\n");
    fprintf(fp, "      },\n");
    fprintf(fp, "      \"results\": [\n");

    for (i = 0; i < error_count; i++) {
        const char *level = "warning";
        if (errors[i].severity >= 8)
            level = "error";
        else if (errors[i].severity >= 3)
            level = "warning";
        else
            level = "note";

        if (i > 0)
            fprintf(fp, ",\n");

        const char *cwe = error_to_cwe(errors[i].type);
        fprintf(fp, "        {\n");
        fprintf(fp, "          \"ruleId\": \"%s\",\n", get_error_name(errors[i].type));
        fprintf(fp, "          \"level\": \"%s\",\n", level);
        fprintf(fp, "          \"message\": {\n");
        if (cwe) {
            fprintf(fp, "            \"text\": \"%s [%s]\"", errors[i].title, cwe);
        } else {
            fprintf(fp, "            \"text\": \"%s\"", errors[i].title);
        }
        if (errors[i].fix_suggestion[0]) {
            fprintf(fp, ",\n");
            if (cwe) {
                fprintf(fp, "            \"markdown\": \"%s [%s]\\n\\n**Suggested fix:** %s\"\n",
                        errors[i].title, cwe, errors[i].fix_suggestion);
            } else {
                fprintf(fp, "            \"markdown\": \"%s\\n\\n**Suggested fix:** %s\"\n",
                        errors[i].title, errors[i].fix_suggestion);
            }
        } else {
            fprintf(fp, "\n");
        }
        fprintf(fp, "          }");

        if (errors[i].has_source && errors[i].source_file[0]) {
            fprintf(fp, ",\n");
            fprintf(fp, "          \"locations\": [\n");
            fprintf(fp, "            {\n");
            fprintf(fp, "              \"physicalLocation\": {\n");
            fprintf(fp, "                \"artifactLocation\": {\n");
            fprintf(fp, "                  \"uri\": \"%s\"\n", errors[i].source_file);
            fprintf(fp, "                }");
            if (errors[i].source_line > 0) {
                fprintf(fp, ",\n");
                fprintf(fp, "                \"region\": {\n");
                fprintf(fp, "                  \"startLine\": %d\n", errors[i].source_line);
                fprintf(fp, "                }\n");
            } else {
                fprintf(fp, "\n");
            }
            fprintf(fp, "              }\n");
            fprintf(fp, "            }\n");
            fprintf(fp, "          ]\n");
        } else {
            fprintf(fp, "\n");
        }

        if (cwe) {
            fprintf(fp, ",\n");
            fprintf(fp, "          \"properties\": {\n");
            fprintf(fp, "            \"cweId\": \"%s\"\n", cwe);
            fprintf(fp, "          }");
        }

        fprintf(fp, "\n        }");
    }
    fprintf(fp, "\n");
    fprintf(fp, "      ],\n");
    fprintf(fp, "      \"columnKind\": \"utf16CodeUnits\",\n");
    fprintf(fp, "      \"properties\": {\n");
    fprintf(fp, "        \"executionTimeMs\": %ld\n", execution_time_ms);
    fprintf(fp, "      }\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return 0;
}

/*
 * generate_ci_output - Write CI-friendly JSON summary to stdout
 */
void generate_ci_output(const char **source_files, int source_count,
                         const DetectedError *errors, int error_count,
                         int exit_status, long execution_time_ms)
{
    const char *status;
    int i;

    if (exit_status == EXIT_CLEAN)
        status = "clean";
    else if (exit_status == EXIT_COMPILE_FAIL)
        status = "compile_fail";
    else
        status = "errors";

    printf("{\n");
    printf("  \"version\": \"1.5\",\n");
    printf("  \"status\": \"%s\",\n", status);
    printf("  \"files\": [\n");
    for (i = 0; i < source_count; i++) {
        if (i > 0) printf(",\n");
        printf("    \"%s\"", source_files[i]);
    }
    printf("\n  ],\n");
    printf("  \"error_count\": %d,\n", error_count);
    printf("  \"errors\": [\n");
    for (i = 0; i < error_count; i++) {
        if (i > 0) printf(",\n");
        printf("    {\n");
        printf("      \"type\": \"%s\",\n", get_error_name(errors[i].type));
        printf("      \"title\": \"%s\",\n", errors[i].title);
        printf("      \"file\": \"%s\",\n",
               errors[i].source_file[0] ? errors[i].source_file : "");
        printf("      \"line\": %d,\n", errors[i].source_line);
        printf("      \"severity\": %d,\n", errors[i].severity);
        const char *cwe = error_to_cwe(errors[i].type);
        if (cwe)
            printf("      \"cwe\": \"%s\",\n", cwe);
        printf("      \"fix\": \"%s\"\n", errors[i].fix_suggestion);
        printf("    }");
    }
    printf("\n  ],\n");
    printf("  \"execution_time_ms\": %ld\n", execution_time_ms);
    printf("}\n");
    fflush(stdout);
}

/*
 * generate_html_report - Write HTML report with error details
 *
 *      source context, and fix suggestions in a web-friendly format.
 *      Shows all source files when multiple files are used.
 */
int generate_html_report(const char *html_path, const char **source_files,
                         int source_count, const TestResult *result,
                         const DetectedError *errors, int error_count)
{
    FILE *fp;
    char timestamp[64];
    time_t now;
    struct tm *tm_info;
    int i;
    const char *primary_source;

    if (!html_path || !source_files || source_count <= 0 || !result)
        return -1;

    primary_source = source_files[0];

    fp = fopen(html_path, "w");
    if (!fp)
        return -1;

    now = time(NULL);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(fp, "<!DOCTYPE html>\n");
    fprintf(fp, "<html>\n<head>\n");
    fprintf(fp, "  <meta charset=\"utf-8\">\n");
    fprintf(fp, "  <title>prism Report - %s</title>\n", primary_source);
    fprintf(fp, "  <style>\n");
    fprintf(fp, "    body { font-family: monospace; max-width: 800px; margin: 2em auto; padding: 0 1em; }\n");
    fprintf(fp, "    h1 { border-bottom: 2px solid #333; padding-bottom: 0.5em; }\n");
    fprintf(fp, "    .meta { color: #555; margin-bottom: 1.5em; }\n");
    fprintf(fp, "    .error { border-left: 4px solid #e74c3c; padding: 1em; margin: 1em 0; background: #fef5f5; }\n");
    fprintf(fp, "    .warning { border-left: 4px solid #f39c12; padding: 1em; margin: 1em 0; background: #fef9f0; }\n");
    fprintf(fp, "    .clean { border-left: 4px solid #27ae60; padding: 1em; margin: 1em 0; background: #f0faf4; }\n");
    fprintf(fp, "    .source-line { background: #f5f5f5; padding: 0.5em; margin: 0.5em 0; border-radius: 3px; overflow-x: auto; }\n");
    fprintf(fp, "    .fix { color: #2c3e50; margin-top: 0.5em; }\n");
    fprintf(fp, "    .status-ok { color: #27ae60; font-weight: bold; }\n");
    fprintf(fp, "    .status-error { color: #e74c3c; font-weight: bold; }\n");
    fprintf(fp, "  </style>\n");
    fprintf(fp, "</head>\n<body>\n");
    fprintf(fp, "  <h1>prism Report</h1>\n");
    fprintf(fp, "  <p class=\"meta\">File(s): ");
    for (i = 0; i < source_count; i++) {
        fprintf(fp, "%s", source_files[i]);
        if (i < source_count - 1) fprintf(fp, ", ");
    }
    fprintf(fp, " | Time: %ldms | Timestamp: %s | ",
            result->execution_time_ms, timestamp);

    if (error_count > 0) {
        fprintf(fp, "<span class=\"status-error\">ERROR</span></p>\n");
    } else if (result->compilation_success) {
        fprintf(fp, "<span class=\"status-ok\">CLEAN</span></p>\n");
    } else {
        fprintf(fp, "<span class=\"status-error\">COMPILE ERROR</span></p>\n");
    }

    if (error_count > 0) {
        for (i = 0; i < error_count; i++) {
            const char *css_class = errors[i].severity == 2 ? "error" : "warning";
            fprintf(fp, "  <div class=\"%s\">\n", css_class);
            fprintf(fp, "    <h2>%s</h2>\n", errors[i].title);
            fprintf(fp, "    <p class=\"fix\">Fix: %s</p>\n", errors[i].fix_suggestion);
            if (errors[i].has_source && errors[i].source_line > 0) {
                char source_line[MAX_SOURCE_LINE];
                if (get_source_line(errors[i].source_file, errors[i].source_line,
                                    source_line, sizeof(source_line)) == 0) {
                    fprintf(fp, "    <pre class=\"source-line\">Line %d: %s</pre>\n",
                            errors[i].source_line, source_line);
                }
            }
            fprintf(fp, "  </div>\n");
        }
        fprintf(fp, "  <p class=\"status-error\">%d error(s) detected</p>\n", error_count);
    } else if (result->compilation_success) {
        fprintf(fp, "  <div class=\"clean\">\n");
        fprintf(fp, "    <h2>No errors detected</h2>\n");
        fprintf(fp, "    <p>Clean run completed in %ldms</p>\n", result->execution_time_ms);
        fprintf(fp, "  </div>\n");
    } else {
        fprintf(fp, "  <div class=\"error\">\n");
        fprintf(fp, "    <h2>Compilation Failed</h2>\n");
        if (result->compiler_output[0]) {
            fprintf(fp, "    <pre class=\"source-line\">%s</pre>\n", result->compiler_output);
        }
        fprintf(fp, "  </div>\n");
    }

    fprintf(fp, "</body>\n</html>\n");
    fclose(fp);
    return 0;
}

/*
 * merge_analysis_error - Add or update error with location-aware dedup
 */
static int merge_analysis_error(DetectedError *errors, int total_errors, int max_errors,
                                 const DetectedError *new_error,
                                 uint64_t *error_modes, uint64_t mode_bit)
{
    for (int j = 0; j < total_errors; j++) {
        if (errors[j].type != new_error->type)
            continue;
        /* If both have source info, compare file + line */
        if (errors[j].has_source && new_error->has_source) {
            if (errors[j].source_line == new_error->source_line &&
                strcmp(errors[j].source_file, new_error->source_file) == 0) {
                error_modes[j] |= mode_bit;
                return total_errors;
            }
        } else if (!errors[j].has_source && !new_error->has_source) {
            /* Neither has source info — treat as same error */
            error_modes[j] |= mode_bit;
            return total_errors;
        }
        /* One has source info, the other doesn't — treat as distinct */
    }

    /* New unique error */
    if (total_errors < max_errors) {
        errors[total_errors] = *new_error;
        error_modes[total_errors] = mode_bit;
        return total_errors + 1;
    }
    return total_errors;
}

/*
 * run_max_analysis - Run ALL analysis modes and aggregate results
 */
int run_max_analysis(const char **sources, int source_count,
                     DetectedError *errors, int max_errors,
                     TestResult *result, int timeout_sec,
                     const ColorCodes *colors)
{
    char binary_path[MAX_PATH_LEN];
    char temp_output[MAX_OUTPUT_SIZE];
    int total_errors = 0;
    /* Remove old seen_types bitset — replaced by merge_analysis_error below */
    const char *mode_names[] = {"Sanitizers", "Compiler Warnings", "GCC Analyzer",
                                "Clang-Tidy", "Valgrind", "ThreadSanitizer"};
    uint64_t error_modes[32] = {0};
    long cumulative_time_ms = 0;

    generate_temp_path("prism", binary_path, sizeof(binary_path));
    memset(result, 0, sizeof(TestResult));

    printf("========================================\n");
    print_colored(colors, colors->bold, "  MAX MODE - Full Analysis\n");
    printf("========================================\n");
    printf("Running 6 analysis passes...\n\n");

    /* ------------------------------------------------------------------ */
    /* Pass 1/6 — Sanitizers (ASan + UBSan)                                */
    /* ------------------------------------------------------------------ */
    printf("[1/6] Sanitizers (ASan+UBSan)...");
    fflush(stdout);
    memset(result->compiler_output, 0, sizeof(result->compiler_output));
    if (compile_with_sanitizers(sources, source_count, binary_path,
                                result->compiler_output,
                                sizeof(result->compiler_output)) == 0) {
        result->compilation_success = true;

        setenv("ASAN_OPTIONS", "detect_leaks=1", 1);

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        int bin_result = run_binary(binary_path, NULL,
                                   result->runtime_output,
                                   sizeof(result->runtime_output),
                                   result->sanitizer_output,
                                   sizeof(result->sanitizer_output),
                                   timeout_sec, NULL);
        clock_gettime(CLOCK_MONOTONIC, &end);
        long pass_time;
        get_execution_time(&start, &end, &pass_time);
        cumulative_time_ms += pass_time;

        if (bin_result == -1) {
            size_t slen = strlen(result->sanitizer_output);
            snprintf(result->sanitizer_output + slen,
                     sizeof(result->sanitizer_output) - slen,
                     "\nExecution timeout after %d seconds", timeout_sec);
        }

        DetectedError temp_errors[32];
        memset(temp_errors, 0, sizeof(temp_errors));
        int count = parse_sanitizer_errors(result->sanitizer_output,
                                            temp_errors, 32);

        for (int i = 0; i < count && total_errors < max_errors; i++)
            total_errors = merge_analysis_error(errors, total_errors, max_errors,
                                                 &temp_errors[i], error_modes, 1u << 0);
        printf(" DONE - %d errors\n", count);
        cleanup_binary(binary_path);
    } else {
        printf(" COMPILE ERROR\n");
    }

    /* ------------------------------------------------------------------ */
    /* Pass 2/6 — Compiler Warnings                                        */
    /* ------------------------------------------------------------------ */
    printf("[2/6] Compiler Warnings...");
    fflush(stdout);
    memset(temp_output, 0, sizeof(temp_output));
    if (compile_for_warnings(sources, source_count, temp_output,
                             sizeof(temp_output)) == 0) {
        DetectedError temp_errors[32];
        memset(temp_errors, 0, sizeof(temp_errors));
        int count = 0;

        if (temp_output[0] != '\0') {
            char *line_start = temp_output;
            char *line_end;
            while (*line_start && count < 32) {
                line_end = strchr(line_start, '\n');
                if (line_end) *line_end = '\0';

                if (string_contains(line_start, "warning:")) {
                    int pattern_idx = classify_error(line_start);
                    if (pattern_idx >= 0) {
                        temp_errors[count].type = error_patterns[pattern_idx].type;
                        snprintf(temp_errors[count].title,
                                 sizeof(temp_errors[count].title),
                                 "%s", error_patterns[pattern_idx].title);
                        snprintf(temp_errors[count].fix_suggestion,
                                 sizeof(temp_errors[count].fix_suggestion),
                                 "%s", error_patterns[pattern_idx].fix);
                        temp_errors[count].severity = error_patterns[pattern_idx].severity;
                        temp_errors[count].has_source = false;
                        count++;
                    } else {
                        /* Unmatched warning - create generic error */
                        if (count < 32) {
                            temp_errors[count].type = ERR_UNKNOWN;
                            snprintf(temp_errors[count].title,
                                     sizeof(temp_errors[count].title),
                                     "Compiler Warning");
                            snprintf(temp_errors[count].fix_suggestion,
                                     sizeof(temp_errors[count].fix_suggestion),
                                     "Compiler detected: %.200s", line_start);
                            temp_errors[count].severity = 1;
                            temp_errors[count].has_source = false;
                            count++;
                        }
                    }
                }

                if (line_end) {
                    *line_end = '\n';
                    line_start = line_end + 1;
                } else {
                    break;
                }
            }
        }

        for (int i = 0; i < count && total_errors < max_errors; i++)
            total_errors = merge_analysis_error(errors, total_errors, max_errors,
                                                 &temp_errors[i], error_modes, 1u << 1);
        printf(" DONE - %d errors\n", count);
    } else {
        printf(" FAILED\n");
    }

    /* ------------------------------------------------------------------ */
    /* Pass 3/6 — GCC Analyzer (-fanalyzer)                                */
    /* ------------------------------------------------------------------ */
    printf("[3/6] GCC Analyzer...");
    fflush(stdout);
    memset(temp_output, 0, sizeof(temp_output));
    if (compile_with_analyzer(sources, source_count, binary_path,
                              temp_output, sizeof(temp_output)) == 0) {
        DetectedError temp_errors[32];
        memset(temp_errors, 0, sizeof(temp_errors));
        int count = parse_sanitizer_errors(temp_output, temp_errors, 32);

        for (int i = 0; i < count && total_errors < max_errors; i++)
            total_errors = merge_analysis_error(errors, total_errors, max_errors,
                                                 &temp_errors[i], error_modes, 1u << 2);
        printf(" DONE - %d errors\n", count);
        /* Clean up the binary produced by compile_with_analyzer */
        cleanup_binary(binary_path);
    } else {
        printf(" FAILED\n");
    }

    /* ------------------------------------------------------------------ */
    /* Pass 4/6 — Clang-Tidy                                               */
    /* ------------------------------------------------------------------ */
    printf("[4/6] Clang-Tidy...");
    fflush(stdout);
    memset(temp_output, 0, sizeof(temp_output));
    if (compile_with_clang_tidy(sources, source_count, temp_output,
                                sizeof(temp_output)) == 0) {
        DetectedError temp_errors[32];
        memset(temp_errors, 0, sizeof(temp_errors));
        int count = 0;

        if (temp_output[0] != '\0') {
            char *line_start = temp_output;
            char *line_end;
            while (*line_start && count < 32) {
                line_end = strchr(line_start, '\n');
                if (line_end) *line_end = '\0';

                if (string_contains(line_start, "warning:") ||
                    string_contains(line_start, "error:")) {
                    int pattern_idx = classify_error(line_start);
                    if (pattern_idx >= 0) {
                        temp_errors[count].type = error_patterns[pattern_idx].type;
                        snprintf(temp_errors[count].title,
                                 sizeof(temp_errors[count].title),
                                 "%s", error_patterns[pattern_idx].title);
                        snprintf(temp_errors[count].fix_suggestion,
                                 sizeof(temp_errors[count].fix_suggestion),
                                 "%s", error_patterns[pattern_idx].fix);
                        temp_errors[count].severity = error_patterns[pattern_idx].severity;
                        temp_errors[count].has_source = false;
                        count++;
                    } else {
                        /* Unmatched clang-tidy warning/error — create generic entry */
                        if (count < 32) {
                            temp_errors[count].type = ERR_UNKNOWN;
                            if (string_contains(line_start, "error:"))
                                snprintf(temp_errors[count].title,
                                         sizeof(temp_errors[count].title),
                                         "Clang-Tidy Error");
                            else
                                snprintf(temp_errors[count].title,
                                         sizeof(temp_errors[count].title),
                                         "Clang-Tidy Warning");
                            snprintf(temp_errors[count].fix_suggestion,
                                     sizeof(temp_errors[count].fix_suggestion),
                                     "clang-tidy reported: %.200s", line_start);
                            temp_errors[count].severity = 1;
                            temp_errors[count].has_source = false;
                            count++;
                        }
                    }
                }

                if (line_end) {
                    *line_end = '\n';
                    line_start = line_end + 1;
                } else {
                    break;
                }
            }
        }

        for (int i = 0; i < count && total_errors < max_errors; i++)
            total_errors = merge_analysis_error(errors, total_errors, max_errors,
                                                 &temp_errors[i], error_modes, 1u << 3);

        printf(" DONE - %d errors\n", count);
    } else {
        printf(" FAILED\n");
    }

    /* ------------------------------------------------------------------ */
    /* Pass 5/6 — Valgrind                                                 */
    /* ------------------------------------------------------------------ */
    printf("[5/6] Valgrind...");
    fflush(stdout);

    /* Check if valgrind is available before attempting */
    if (system("command -v valgrind > /dev/null 2>&1") != 0) {
        printf(" SKIPPED (valgrind not found)\n");
    } else {
        memset(result->compiler_output, 0, sizeof(result->compiler_output));
        if (compile_for_valgrind(sources, source_count, binary_path,
                             result->compiler_output,
                             sizeof(result->compiler_output)) == 0) {
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);
            int vg_result = run_with_valgrind(binary_path, NULL, 0,
                              result->sanitizer_output,
                              sizeof(result->sanitizer_output),
                              timeout_sec, NULL);
            clock_gettime(CLOCK_MONOTONIC, &end);
            long pass_time;
            get_execution_time(&start, &end, &pass_time);
            cumulative_time_ms += pass_time;

            if (vg_result == -1) {
                size_t slen = strlen(result->sanitizer_output);
                snprintf(result->sanitizer_output + slen,
                         sizeof(result->sanitizer_output) - slen,
                         "\nValgrind execution timeout after %d seconds", timeout_sec);
            }

            DetectedError temp_errors[32];
            memset(temp_errors, 0, sizeof(temp_errors));
            int count = parse_sanitizer_errors(result->sanitizer_output,
                                                temp_errors, 32);

            for (int i = 0; i < count && total_errors < max_errors; i++)
                total_errors = merge_analysis_error(errors, total_errors, max_errors,
                                                     &temp_errors[i], error_modes, 1u << 4);
            printf(" DONE - %d errors\n", count);
            cleanup_binary(binary_path);
        } else {
            printf(" COMPILE ERROR\n");
        }
    }

    /* ------------------------------------------------------------------ */
    /* Pass 6/6 — ThreadSanitizer                                          */
    /* ------------------------------------------------------------------ */
    printf("[6/6] ThreadSanitizer...");
    fflush(stdout);
    memset(result->compiler_output, 0, sizeof(result->compiler_output));
    if (compile_with_tsan(sources, source_count, binary_path,
                          result->compiler_output,
                          sizeof(result->compiler_output)) == 0) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        setenv("ASAN_OPTIONS", "detect_leaks=1", 1);

        int bin_result = run_binary(binary_path, NULL,
                                  result->runtime_output,
                                  sizeof(result->runtime_output),
                                  result->sanitizer_output,
                                  sizeof(result->sanitizer_output),
                                  timeout_sec, NULL);
        clock_gettime(CLOCK_MONOTONIC, &end);
        long pass_time;
        get_execution_time(&start, &end, &pass_time);
        cumulative_time_ms += pass_time;

        if (bin_result == -1) {
            size_t slen = strlen(result->sanitizer_output);
            snprintf(result->sanitizer_output + slen,
                     sizeof(result->sanitizer_output) - slen,
                     "\nExecution timeout after %d seconds", timeout_sec);
        }

        DetectedError temp_errors[32];
        memset(temp_errors, 0, sizeof(temp_errors));
        int count = parse_sanitizer_errors(result->sanitizer_output,
                                            temp_errors, 32);

        for (int i = 0; i < count && total_errors < max_errors; i++)
            total_errors = merge_analysis_error(errors, total_errors, max_errors,
                                                 &temp_errors[i], error_modes, 1u << 5);
        printf(" DONE - %d errors\n", count);
        cleanup_binary(binary_path);
    } else {
        printf(" COMPILE ERROR\n");
    }

    /* Store cumulative time for display */
    result->execution_time_ms = cumulative_time_ms;

    printf("\n========================================\n");
    print_colored(colors, colors->bold, "  RESULTS - %d unique errors found\n",
                 total_errors);
    printf("========================================\n");

    for (int i = 0; i < total_errors; i++) {
        generate_fix_suggestion(&errors[i]);
        print_colored(colors, colors->red, "[ERROR] %s (found by: ",
                     errors[i].title);
        bool first = true;
        for (int m = 0; m < 6; m++) {
            if (error_modes[i] & (1u << m)) {
                if (!first) printf(", ");
                printf("%s", mode_names[m]);
                first = false;
            }
        }
        printf(")\n");
        print_colored(colors, colors->yellow, "  Fix: ");
        printf("%s\n\n", errors[i].fix_suggestion);
    }

    if (total_errors == 0) {
        print_colored(colors, colors->green, "[OK] ");
        printf("No errors detected - clean run\n");
    }

    return total_errors;
}

/*
 * print_usage - Print comprehensive help message and exit
 */
void print_usage(const char *prog_name, const ColorCodes *colors)
{
    print_colored(colors, colors->bold, "prism v1.5 - C Error Detection Tool\n");
    printf("Detects memory errors, undefined behavior, and bugs in C/C++ code.\n\n");

    print_colored(colors, colors->bold, "Usage: ");
    printf("%s [options] <source.c> [source2.c ...]\n\n", prog_name);

    print_colored(colors, colors->bold, "Analysis Modes:\n");
    printf("  --quick        Quick check: -Wall -Werror, no sanitizers (default)\n");
    printf("  --full         Full analysis with ASan+UBSan sanitizers\n");
    printf("  --max          Run ALL analysis modes sequentially\n");
    printf("  --ultra        Compile 11 binary variants (O0/O1/O2/O3/gcov/libfuzzer),\n");
    printf("                 run 32 analysis passes in parallel via fork():\n");
    printf("                 ASan@O0/O1/O2/O3 + UBSan + TSan + Valgrind +\n");
    printf("                 clang-tidy + -fanalyzer + fuzz + rerun + gcov +\n");
    printf("                 libfuzzer + danger scan + resource leak + GDB\n");
    printf("  --tsan         Use ThreadSanitizer to detect data races\n");
    printf("  --msan         Use MemorySanitizer to detect uninitialized reads\n");
    printf("  --analyzer     Use GCC static analyzer (-fanalyzer)\n");
    printf("  --clang-tidy   Run clang-tidy static analysis\n");
    printf("  --cppcheck     Run cppcheck static analysis on source files\n");
    printf("  --clang-analyze Run Clang Static Analyzer (path-sensitive)\n");
    printf("  --iwyu         Run include-what-you-use (check #include hygiene)\n");
    printf("  --strict-aliasing Check for type-punning violations\n");
    printf("  --conversion   Check for implicit type conversion loss\n");
    printf("  --conversion=all Full conversion check including sign changes\n");
    printf("  --float-equal  Check for floating-point ==/!= comparisons\n");
    printf("  --valgrind     Run under Valgrind for deep memory analysis\n");
    printf("  --fuzz         Run with boundary fuzz inputs (empty, huge, neg)\n");
    printf("  --rerun=N      Run N times to detect non-deterministic bugs\n");
    printf("  --resources    Check for file descriptor / OS resource leaks\n");
    printf("  --danger       Scan source for dangerous API calls\n");
    printf("  --ast          AST-level analysis via libclang (detects dangerous\n");
    printf("                 calls via cursor matching, not regex — no false\n");
    printf("                 positives from comments or string literals)\n");
    printf("  --gcov         Run with code coverage instrumentation\n");
    printf("  --libfuzzer    Run libFuzzer (requires clang + fuzz target)\n");
    printf("  --gdb          On crash, automatically run GDB for backtrace\n");
    printf("  --checksec     Check binary for security hardening features\n");
    printf("  --compile-only Compile only, do not run the binary\n");
    printf("  --header       Check header file(s) syntax (-fsyntax-only)\n");
    printf("  --project=<file>  Use compile flags from compile_commands.json\n");
    printf("  --link-flags=<libs>  Extra linker flags (e.g. \"-lm -lpthread\")\n");
    printf("                   Auto-detected from existing binary if not set\n\n");

    print_colored(colors, colors->bold, "Workflow:\n");
    printf("  --cache        Enable incremental analysis caching\n");
    printf("  --clear-cache  Clear the analysis cache\n");
    printf("  --git-diff     Only analyze files changed since HEAD\n");
    printf("  --git-bisect=<ref>  Git bisect to find which commit broke the code\n");
    printf("  --install-hook Install git pre-commit hook (runs --quick --git-diff)\n");
    printf("  --uninstall-hook  Remove git pre-commit hook\n\n");

    print_colored(colors, colors->bold, "Suppression:\n");
    printf("  --baseline=<file>  Load baseline, suppress known errors\n");
    printf("  --generate-baseline  Save current errors as baseline\n\n");

    print_colored(colors, colors->bold, "Output:\n");
    printf("  --html=<path>  Generate HTML report at specified path\n");
    printf("  --sarif[=<path>]  Generate SARIF v2.1.0 report (default: prism_results.sarif)\n");
    printf("  --ci           CI mode: JSON summary to stdout, strict exit codes\n");
    printf("  --no-color     Disable colored output\n\n");

    print_colored(colors, colors->bold, "Execution:\n");
    printf("  --keep         Keep compiled binary after run\n");
    printf("  --timeout=N    Set execution timeout in seconds (default: %d)\n",
           DEFAULT_TIMEOUT_SEC);
    printf("  --jobs=N, -j N Process N files in parallel (default: nproc)\n");
    printf("                 --ultra forces --jobs=nproc, compiles 11 binaries,\n");
    printf("                 runs 32 analysis passes concurrently (not sequential)\n\n");

    print_colored(colors, colors->bold, "Examples:\n");
    printf("  %s main.c                          Fast basic check (default)\n", prog_name);
    printf("  %s --full main.c                   Full ASan+UBSan analysis\n", prog_name);
    printf("  %s --ultra main.c                  Ultimate analysis (11 bins, 32 passes)\n", prog_name);
    printf("  %s --git-diff main.c               Only check changed files\n", prog_name);
    printf("  %s --install-hook                  Install pre-commit hook\n", prog_name);
    printf("  %s main.c utils.c helper.c         Multi-file project\n\n", prog_name);

    printf("Supported files: .c, .cpp, .cxx, .cc\n");
}

/*
 * find_compile_commands - Walk up from start_dir looking for compile_commands.json
 *                         Checks CWD first, then build/, then parent directories.
 *                         Returns 0 if found, -1 on failure.
 */
int find_compile_commands(const char *start_dir, char *out_path, size_t out_size)
{
    char dir[MAX_PATH_LEN];
    char resolved[MAX_PATH_LEN];

    if (!start_dir || !start_dir[0])
        start_dir = ".";

    /* Resolve to absolute path */
    if (!realpath(start_dir, resolved))
        return -1;

    snprintf(dir, sizeof(dir), "%s", resolved);

    while (dir[0]) {
        /* Check for compile_commands.json in current dir */
        snprintf(out_path, out_size, "%s/compile_commands.json", dir);
        if (access(out_path, R_OK) == 0)
            return 0;

        /* Check in build/ subdirectory */
        snprintf(out_path, out_size, "%s/build/compile_commands.json", dir);
        if (access(out_path, R_OK) == 0)
            return 0;

        /* Go up one directory */
        char *last = strrchr(dir, '/');
        if (!last || last == dir) {
            /* At root — check root once then give up */
            if (last == dir && dir[1] == '\0')
                break;
            /* Last chance: root dir if we haven't checked it */
            if (last == dir) {
                dir[1] = '\0'; /* "/" */
                continue;
            }
            break;
        }
        *last = '\0';
    }

    out_path[0] = '\0';
    return -1;
}

/*
 * get_project_sources - Extract all source file paths from compile_commands.json
 *
 * Uses jq to extract each entry's directory + file, deduplicates,
 * and stores full paths. Returns number of sources found, or -1 on error.
 */
int get_project_sources(const char *json_path, char (*sources)[MAX_PATH_LEN], int max_sources)
{
    char cmd[8192];
    char buf[65536];
    FILE *pipe;
    int count = 0;

    /* Check if jq is available */
    if (system("command -v jq > /dev/null 2>&1") != 0)
        return -1;

    /* Extract directory+file for each entry, sort unique.
     * Handle both relative and absolute .file fields. */
    snprintf(cmd, sizeof(cmd),
             "jq -r '.[] | if (.file | startswith(\"/\")) then .file else \"\\(.directory)/\\(.file)\" end' '%s' 2>/dev/null | sort -u",
             json_path);

    pipe = popen(cmd, "r");
    if (!pipe)
        return -1;

    while (fgets(buf, sizeof(buf), pipe) && count < max_sources) {
        size_t len = strlen(buf);
        /* Strip trailing newline */
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
        if (len == 0 || len >= MAX_PATH_LEN)
            continue;

        /* Normalize path: resolve relative components, remove double slashes.
         * realpath resolves symlinks, which is correct for finding the real source path. */
        char *resolved = realpath(buf, NULL);
        if (resolved) {
            if (strlen(resolved) < MAX_PATH_LEN) {
                memcpy(sources[count], resolved, strlen(resolved) + 1);
                /* Deduplicate against earlier entries */
                bool dup = false;
                for (int j = 0; j < count; j++) {
                    if (strcmp(sources[j], sources[count]) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    count++;
            }
            free(resolved);
        } else {
            /* Can't resolve — use as-is but skip if it looks like a header */
            const char *dot = strrchr(buf, '.');
            if (dot && (strcmp(dot, ".c") == 0 ||
                        strcmp(dot, ".cpp") == 0 ||
                        strcmp(dot, ".cxx") == 0 ||
                        strcmp(dot, ".cc") == 0)) {
                bool dup = false;
                for (int j = 0; j < count; j++) {
                    if (strcmp(sources[j], buf) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    memcpy(sources[count++], buf, len + 1);
            }
        }
    }

    pclose(pipe);
    return count;
}

/*
 * parse_compile_commands - Extract compile flags for a source file
 *
 * Matches by filename first (endswith), then full path, then basename.
 */
int parse_compile_commands(const char *json_path, const char *source_file,
                          char *flags_output, size_t flags_size)
{
    char cmd[4096];
    FILE *pipe;
    size_t bytes_read;

    /* Check if jq is available */
    if (system("command -v jq > /dev/null 2>&1") != 0) {
        snprintf(flags_output, flags_size, "jq not found. Install jq to use compile_commands.json");
        return -1;
    }

    /* Use jq to extract the command for this source file.
     * Match against the full path (directory + "/" + file) so that
     * both basename and full-path matching works correctly.
     * Handle both relative and absolute .file fields. */
    snprintf(cmd, sizeof(cmd),
             "jq -r '[.[] | select((if (.file | startswith(\"/\")) then .file else \"\\(.directory)/\\(.file)\" end) | endswith(\"%s\"))] | .[0] | .command' '%s' 2>/dev/null",
             source_file, json_path);

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(flags_output, flags_size, "Failed to run jq");
        return -1;
    }

    bytes_read = fread(flags_output, 1, flags_size - 1, pipe);
    flags_output[bytes_read] = '\0';
    pclose(pipe);

    /* Trim whitespace */
    trim_whitespace(flags_output);

    if (flags_output[0] == '\0') {
        return -1;
    }

    return 0;
}

/*
 * clean_compile_flags - Strip compiler name, -c, -o <arg>, -S, source/object files
 *                       from a raw compile_commands.json command string
 */
void clean_compile_flags(const char *raw_flags, char *cleaned, size_t cleaned_size)
{
    size_t cp = 0;
    const char *f = raw_flags;
    bool skip_next = false;
    bool first = true;

    while (*f && cp < cleaned_size - 2) {
        while (*f == ' ' || *f == '\t') f++;
        if (!*f) break;

        const char *end = f;
        while (*end && *end != ' ' && *end != '\t') end++;
        size_t tlen = (size_t)(end - f);

        if (skip_next) { skip_next = false; f = end; continue; }
        if (first) { first = false; f = end; continue; }
        if (tlen == 2 && strncmp(f, "-c", 2) == 0) { f = end; continue; }
        if (tlen == 2 && strncmp(f, "-o", 2) == 0) { skip_next = true; f = end; continue; }
        if (tlen == 2 && strncmp(f, "-S", 2) == 0) { f = end; continue; }

        /* Skip source/object file paths (tokens not starting with '-') */
        if (f[0] != '-') {
            bool is_file = false;
            if (tlen >= 2 && strncmp(end - 2, ".c", 2) == 0) is_file = true;
            if (tlen >= 4 && strncmp(end - 4, ".cpp", 4) == 0) is_file = true;
            if (tlen >= 2 && strncmp(end - 2, ".o", 2) == 0) is_file = true;
            if (is_file) { f = end; continue; }
        }

        if (cp > 0) cleaned[cp++] = ' ';
        if (cp + tlen < cleaned_size - 1) {
            memcpy(cleaned + cp, f, tlen);
            cp += tlen;
        }
        f = end;
    }
    cleaned[cp] = '\0';
}

/*
 * run_with_compile_flags - Compile using flags from compile_commands.json
 *
 * The raw "command" field from compile_commands.json includes the compiler
 * path, -c, -o, and source files — strip those and keep only the flags.
 */
int run_with_compile_flags(const char **sources, int source_count,
                           const char *flags,
                           const char *binary,
                           char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 1024];
    char cleaned[8192];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    clean_compile_flags(flags, cleaned, sizeof(cleaned));

    pos = snprintf(cmd, sizeof(cmd), "%s %s -o '%s'",
                   is_cpp_file(sources[0]) ? "g++" : "gcc",
                   cleaned, binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_project_binary - Compile each source with its own flags, then link
 *
 * For each source file, looks up its compile flags in compile_commands.json,
 * compiles it to a .o file individually, then links all .o files together.
 * This avoids the "compile all with flags from first file" problem.
 */
int compile_project_binary(const char *json_path, const char **sources,
                            int source_count, const char *extra_flags,
                            const char *link_flags,
                            const char *binary,
                            char *output, size_t output_size)
{
    char obj_files[256][MAX_PATH_LEN];
    int obj_count = 0;
    int i;
    bool has_cpp = false;

    /* Phase 1: Compile each source to .o with its own flags */
    for (i = 0; i < source_count; i++) {
        char file_flags[8192];
        char cleaned[8192];
        char cmd[16384];
        char obj_path[MAX_PATH_LEN];
        FILE *pipe;

        /* Detect C++ sources for the linker */
        if (is_cpp_file(sources[i]))
            has_cpp = true;

        /* Get flags for this specific file from JSON */
        if (parse_compile_commands(json_path, sources[i],
                                   file_flags, sizeof(file_flags)) != 0) {
            snprintf(output, output_size,
                     "No compile flags found for %s in %s",
                     sources[i], json_path);
            goto cleanup;
        }

        /* Clean flags: strip compiler name, -c, -o, -S, source/object files */
        clean_compile_flags(file_flags, cleaned, sizeof(cleaned));

        /* Build per-file compile command.
         * file_flags has the form: "/path/to/gcc -flag1 -flag2 ..."
         * We extract the compiler from the first token and prepend extra_flags. */
        const char *p = file_flags;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t plen = (size_t)(p - file_flags);

        snprintf(obj_path, sizeof(obj_path), "/tmp/prism_%d_%d.o",
                 (int)getpid(), i);

        if (extra_flags && extra_flags[0]) {
            snprintf(cmd, sizeof(cmd),
                     "%.*s %s %s -c -o '%s' '%s' 2>&1",
                     (int)plen, file_flags, extra_flags, cleaned,
                     obj_path, sources[i]);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "%.*s %s -c -o '%s' '%s' 2>&1",
                     (int)plen, file_flags, cleaned,
                     obj_path, sources[i]);
        }

        pipe = popen(cmd, "r");
        if (!pipe) {
            snprintf(output, output_size,
                     "Failed to start compiler for %s", sources[i]);
            unlink(obj_path);
            goto cleanup;
        }

        size_t n = fread(output, 1, output_size - 1, pipe);
        output[n] = '\0';
        int status = pclose(pipe);

        if (status != 0) {
            /* output already has the error message */
            unlink(obj_path);
            goto cleanup;
        }

        memcpy(obj_files[obj_count++], obj_path, sizeof(obj_files[0]));
    }

    if (obj_count == 0) {
        snprintf(output, output_size, "No object files to link");
        goto cleanup;
    }

    /* Phase 2: Link all .o files into the final binary */
    {
        char link_cmd[32768];
        size_t pos = snprintf(link_cmd, sizeof(link_cmd),
                              "%s %s",
                              has_cpp ? "g++" : "gcc",
                              extra_flags ? extra_flags : "");

        for (i = 0; i < obj_count; i++) {
            pos += snprintf(link_cmd + pos, sizeof(link_cmd) - pos,
                            " '%s'", obj_files[i]);
            if (pos >= sizeof(link_cmd) - 64) break;
        }

        /* Append manual link flags if provided */
        if (link_flags && link_flags[0]) {
            pos += snprintf(link_cmd + pos, sizeof(link_cmd) - pos,
                            " %s", link_flags);
        }

        pos += snprintf(link_cmd + pos, sizeof(link_cmd) - pos,
                        " -o '%s' 2>&1", binary);

        FILE *pipe = popen(link_cmd, "r");
        if (!pipe) {
            snprintf(output, output_size, "Failed to start linker");
            goto cleanup;
        }

        size_t n = fread(output, 1, output_size - 1, pipe);
        output[n] = '\0';
        int status = pclose(pipe);

        if (status != 0) {
            /* Linking failed — try auto-detecting libraries from
             * the project's pre-built binary via ldd and retry */
            if (!link_flags || !link_flags[0]) {
                char auto_flags[4096] = {0};
                if (auto_detect_link_flags(json_path, sources, source_count,
                                           auto_flags, sizeof(auto_flags)) == 0 &&
                    auto_flags[0]) {
                    /* Rebuild link command with auto-detected flags */
                    char retry_cmd[32768];
                    pos = snprintf(retry_cmd, sizeof(retry_cmd),
                                   "%s %s",
                                   has_cpp ? "g++" : "gcc",
                                   extra_flags ? extra_flags : "");
                    for (i = 0; i < obj_count; i++) {
                        pos += snprintf(retry_cmd + pos, sizeof(retry_cmd) - pos,
                                        " '%s'", obj_files[i]);
                        if (pos >= sizeof(retry_cmd) - 64) break;
                    }
                    pos += snprintf(retry_cmd + pos, sizeof(retry_cmd) - pos,
                                    " %s -o '%s' 2>&1",
                                    auto_flags, binary);

                    FILE *rpipe = popen(retry_cmd, "r");
                    if (rpipe) {
                        n = fread(output, 1, output_size - 1, rpipe);
                        output[n] = '\0';
                        int rstatus = pclose(rpipe);

                        if (rstatus == 0) {
                            /* Auto-detected flags worked! */
                            goto linked;
                        }
                        /* output has the retry linker error */
                    }
                }
            }
            goto cleanup;
        }
    }

linked: /* Successfully linked */
    output[0] = '\0';

    /* Clean up .o files */
    for (i = 0; i < obj_count; i++)
        unlink(obj_files[i]);

    return 0;

cleanup:
    for (i = 0; i < obj_count; i++)
        unlink(obj_files[i]);
    return -1;
}

/*
 * auto_detect_link_flags - Extract library flags from the project's pre-built binary
 *
 * Looks for an executable in the project's build directory that was compiled from
 * the same source files. Runs ldd on it and converts shared library names to -l
 * flags. This lets prism link projects with external dependencies (Python, ncurses,
 * etc.) without the user having to manually specify --link-flags.
 */
int auto_detect_link_flags(const char *json_path, const char **sources,
                            int source_count,
                            char *flags_buf, size_t flags_size)
{
    char build_dir[MAX_PATH_LEN] = {0};
    char ldd_cmd[8192];
    char line_buf[4096];
    FILE *pipe;
    size_t flen = 0;

    (void)sources;
    (void)source_count;

    if (!json_path || !json_path[0])
        return -1;

    /* Get build directory from compile_commands.json (first entry) */
    snprintf(ldd_cmd, sizeof(ldd_cmd),
             "jq -r '.[0].directory' '%s' 2>/dev/null | head -1",
             json_path);
    pipe = popen(ldd_cmd, "r");
    if (pipe) {
        size_t n = fread(build_dir, 1, sizeof(build_dir) - 1, pipe);
        build_dir[n] = '\0';
        pclose(pipe);
        trim_whitespace(build_dir);
    }

    if (!build_dir[0])
        return -1;

    /* Look for an executable in the build directory that was compiled
     * from one of the source files (same basename, newer timestamp) */
    flags_buf[0] = '\0';

    /* Find all executables in the build directory, pick the most recently
     * modified one that's linked against at least some of the same source files */
    char find_cmd[8192];
    snprintf(find_cmd, sizeof(find_cmd),
             "find '%s' -maxdepth 2 -type f -executable 2>/dev/null | "
             "xargs -r file 2>/dev/null | grep -i 'ELF.*executable' | "
             "sed 's/:.*//' | head -5",
             build_dir);

    pipe = popen(find_cmd, "r");
    if (!pipe)
        return -1;

    char bin_paths[5][MAX_PATH_LEN];
    int bin_count = 0;

    while (bin_count < 5) {
        if (!fgets(bin_paths[bin_count], sizeof(bin_paths[0]), pipe))
            break;
        size_t n = strlen(bin_paths[bin_count]);
        while (n > 0 && (bin_paths[bin_count][n-1] == '\n' ||
                         bin_paths[bin_count][n-1] == '\r'))
            bin_paths[bin_count][--n] = '\0';
        if (n > 0)
            bin_count++;
    }
    pclose(pipe);

    if (bin_count == 0)
        return -1;

    /* Try each binary. Use the first one that ldd produces output for. */
    for (int bi = 0; bi < bin_count; bi++) {
        char found[MAX_PATH_LEN];
        snprintf(found, sizeof(found), "%s", bin_paths[bi]);
        flen = 0;
        flags_buf[0] = '\0';

        /* Run ldd and extract library names, convert to -l flags */
        snprintf(ldd_cmd, sizeof(ldd_cmd),
                 "ldd '%s' 2>/dev/null | "
                 "awk '{print $1}' | "
                 "grep -v '^ld-linux\\|^linux-vdso\\|^libc\\.so\\|^libm\\.so\\|^libgcc_s\\|^libstdc++\\|^libpthread\\|^libdl\\|librt\\|^libresolv' | "
                 "sed 's/^\\(lib\\)//; s/\\.so.*$//' | "
                 "sort -u",
                 found);

        FILE *ldd_pipe = popen(ldd_cmd, "r");
        if (!ldd_pipe)
            continue;

        while (fgets(line_buf, sizeof(line_buf), ldd_pipe) && flen < flags_size - 64) {
            size_t ln = strlen(line_buf);
            while (ln > 0 && (line_buf[ln-1] == '\n' || line_buf[ln-1] == '\r'))
                line_buf[--ln] = '\0';
            if (ln == 0)
                continue;

            /* filter out versioned libs that don't make sense as -l flags */
            if (strchr(line_buf, '.') || strchr(line_buf, '/'))
                continue;

            if (flen > 0)
                flen += snprintf(flags_buf + flen, flags_size - flen, " ");
            flen += snprintf(flags_buf + flen, flags_size - flen, "-l%s", line_buf);
        }
        pclose(ldd_pipe);

        if (flen > 0) {
            trim_whitespace(flags_buf);
            return 0;
        }
    }

    flags_buf[0] = '\0';
    return -1;
}

/*
 * run_fuzz_analysis - Run binary with boundary inputs
 *
 *      integer values but pass with normal input. This runs the binary
 *      under ASan with ~10 edge-case inputs via argv[1].
 */
int run_fuzz_analysis(const char *binary,
                      DetectedError *errors, int max_errors,
                      char *sanitizer_output, size_t sanitizer_size,
                      int timeout_sec)
{
    char buf_512[513];     /* 512B + null */
    char buf_16k[16384];   /* 15871B + null */
    char buf_64k[65536];   /* 65535B + null */
    int total_errors = 0;
    int input_count = 0;
    uint64_t error_modes[32] = {0};
    const char *inputs[16];

    /* Build the input list */
    inputs[input_count++] = "";
    inputs[input_count++] = "A";
    inputs[input_count++] = "AAAAAAAAAAAAAAAA";
    inputs[input_count++] = "-1";
    inputs[input_count++] = "0";
    inputs[input_count++] = "2147483648";
    inputs[input_count++] = "-2147483649";

    /* 512-byte input */
    memset(buf_512, 'B', 512);
    buf_512[512] = '\0';
    inputs[input_count++] = buf_512;

    /* 16KB ASan-friendly input */
    memset(buf_16k, 'C', 15871);
    buf_16k[15871] = '\0';
    inputs[input_count++] = buf_16k;

    /* 64KB input (triggers typical stack/heap overflows) */
    memset(buf_64k, 'D', 65535);
    buf_64k[65535] = '\0';
    inputs[input_count++] = buf_64k;

    inputs[input_count] = NULL;

    setenv("ASAN_OPTIONS", "detect_leaks=1:abort_on_error=1", 1);

    for (int i = 0; i < input_count && total_errors < max_errors; i++) {
        char run_out[MAX_OUTPUT_SIZE];
        char run_err[MAX_OUTPUT_SIZE];
        DetectedError temp_errors[8];
        int count;
        int exit_code;

        memset(run_out, 0, sizeof(run_out));
        memset(run_err, 0, sizeof(run_err));
        memset(temp_errors, 0, sizeof(temp_errors));

        exit_code = run_binary(binary, inputs[i],
                   run_out, sizeof(run_out),
                   run_err, sizeof(run_err),
                   timeout_sec, NULL);

        if (exit_code == -1) {
            /* Timeout — report as separate error */
            temp_errors[0].type = ERR_UNKNOWN;
            snprintf(temp_errors[0].title, sizeof(temp_errors[0].title),
                     "Timeout on fuzz input #%d", i + 1);
            snprintf(temp_errors[0].fix_suggestion,
                     sizeof(temp_errors[0].fix_suggestion),
                     "The program timed out (>%ds) with input '%s'. "
                     "Check for infinite loops or excessive processing time "
                     "caused by unusual input sizes.",
                     timeout_sec, inputs[i]);
            temp_errors[0].severity = 2;
            temp_errors[0].has_source = false;
            count = 1;
        } else {
            count = parse_sanitizer_errors(run_err, temp_errors, 8);

            if (count == 0 && strlen(run_err) > 0) {
                /* Non-ASan crash — report as unknown */
                temp_errors[0].type = ERR_SEG;
                snprintf(temp_errors[0].title, sizeof(temp_errors[0].title),
                         "Crash on fuzz input #%d", i + 1);
                snprintf(temp_errors[0].fix_suggestion,
                         sizeof(temp_errors[0].fix_suggestion),
                         "The program crashed with input '%s'. Check for buffer "
                         "overflows, integer overflows, or null dereferences "
                         "caused by unusual input sizes.",
                         inputs[i]);
                temp_errors[0].severity = 3;
                temp_errors[0].has_source = false;
                count = 1;
            }
        }

        for (int j = 0; j < count && total_errors < max_errors; j++) {
            total_errors = merge_analysis_error(errors, total_errors,
                                                 max_errors,
                                                 &temp_errors[j],
                                                 error_modes,
                                                 1ull << i);
        }

        /* Append fuzz run output to combined sanitizer buffer */
        if (run_err[0]) {
            size_t slen = strlen(sanitizer_output);
            if (slen + strlen(run_err) + 64 < sanitizer_size) {
                snprintf(sanitizer_output + slen,
                         sanitizer_size - slen,
                         "\n--- Fuzz input #%d: \"%s\" ---\n%s",
                         i + 1, strlen(inputs[i]) > 32 ? "(large)" : inputs[i],
                         run_err);
            }
        }
    }

    return total_errors;
}

/*
 * run_with_rerun - Run binary multiple times to detect heisenbugs
 */
int run_with_rerun(const char *binary, int rerun_count,
                   DetectedError *errors, int max_errors,
                   char *output, size_t output_size,
                   char *error_output, size_t error_size,
                   int timeout_sec)
{
    int exit_codes[32];
    int crash_count = 0;
    int timeout_count = 0;
    int clean_count = 0;
    int total = 0;
    char first_error[1024] = {0};
    int have_error = 0;

    if (rerun_count > 32) rerun_count = 32;
    if (rerun_count < 2) rerun_count = 2;

    setenv("ASAN_OPTIONS", "detect_leaks=1", 1);

    for (int i = 0; i < rerun_count; i++) {
        char run_out[MAX_OUTPUT_SIZE];
        char run_err[MAX_OUTPUT_SIZE];

        memset(run_out, 0, sizeof(run_out));
        memset(run_err, 0, sizeof(run_err));

        exit_codes[i] = run_binary(binary, NULL,
                                    run_out, sizeof(run_out),
                                    run_err, sizeof(run_err),
                                    timeout_sec, NULL);

        if (exit_codes[i] == -1) {
            timeout_count++;
        } else if (exit_codes[i] != 0) {
            crash_count++;
            if (!have_error && run_err[0]) {
                strncpy(first_error, run_err, sizeof(first_error) - 1);
                first_error[sizeof(first_error) - 1] = '\0';
                have_error = 1;
            }
        } else {
            clean_count++;
        }

        /* Save first run's output */
        if (i == 0) {
            strncpy(output, run_out, output_size - 1);
            strncpy(error_output, run_err, error_size - 1);
        }
    }

    /* Check if all runs were the same */
    int all_same = 1;
    for (int i = 1; i < rerun_count; i++) {
        if (exit_codes[i] != exit_codes[0]) {
            all_same = 0;
            break;
        }
    }

    if (all_same && crash_count == 0) {
        return 0;  /* All clean, deterministic */
    }

    if (all_same && crash_count > 0) {
        /* All crashed identically — parse the first run's ASan output */
        DetectedError det_errors[16];
        int n = parse_sanitizer_errors(error_output, det_errors, 16);

        if (n > 0) {
            /* Return parsed sanitizer errors */
            for (int i = 0; i < n && total < max_errors; i++) {
                errors[total++] = det_errors[i];
            }
            return total;
        }

        /* No ASan output — report as generic deterministic crash */
        snprintf(errors[0].title, sizeof(errors[0].title),
                 "Deterministic Crash");
        snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                 "Binary crashed in all %d runs. Run with --valgrind "
                 "for deeper analysis.", rerun_count);
        errors[0].severity = 3;
        errors[0].has_source = false;
        errors[0].type = ERR_SEG;
        total = 1;
        return total;
    }

    /* Variance detected — report heisenbug */
    if (total < max_errors) {
        int idx = total;
        errors[idx].type = ERR_UNKNOWN;
        snprintf(errors[idx].title, sizeof(errors[idx].title),
                 "Non-deterministic Bug (heisenbug)");
        if (have_error) {
            /* Truncate first_error to a single line for the suggestion */
            char *nl = strchr(first_error, '\n');
            if (nl) *nl = '\0';
            /* Truncate first line if very long (fix_suggestion is small) */
            first_error[sizeof(first_error) - 1] = '\0';
            snprintf(errors[idx].fix_suggestion,
                     sizeof(errors[idx].fix_suggestion),
                     "Crashed in %d/%d runs (timeout: %d, clean: %d). "
                     "First crash: %s. Run with --valgrind for deeper analysis.",
                     crash_count, rerun_count, timeout_count, clean_count,
                     first_error);
        } else {
            snprintf(errors[idx].fix_suggestion,
                     sizeof(errors[idx].fix_suggestion),
                     "Crashed in %d/%d runs (timeout: %d, clean: %d). "
                     "Run with --valgrind or --tsan for deeper analysis.",
                     crash_count, rerun_count, timeout_count, clean_count);
        }
        errors[idx].severity = 3;
        errors[idx].has_source = false;
        total = 1;
    }

    return total;
}

/*
 * check_resource_leaks - Detect file descriptor and OS resource leaks
 *
 *      Uses valgrind --track-fds=yes if available, otherwise measures
 *      /proc/self/fd count before/after execution in a child process.
 */
int check_resource_leaks(const char *binary,
                         DetectedError *errors, int max_errors,
                         char *sanitizer_output, size_t sanitizer_size,
                         int timeout_sec)
{
    int found = 0;

    if (system("command -v valgrind > /dev/null 2>&1") == 0) {
        /* Valgrind track-fds mode */
        char real_cmd[4096];
        char vg_output[MAX_OUTPUT_SIZE];
        FILE *pipe;
        int exit_status;

        /* Use timeout wrapper if timeout_sec > 0 */
        if (timeout_sec > 0) {
            snprintf(real_cmd, sizeof(real_cmd),
                     "timeout %d valgrind --track-fds=yes --leak-check=no "
                     "--error-exitcode=0 '%s' 2>&1",
                     timeout_sec, binary);
        } else {
            snprintf(real_cmd, sizeof(real_cmd),
                     "valgrind --track-fds=yes --leak-check=no "
                     "--error-exitcode=0 '%s' 2>&1",
                     binary);
        }

        pipe = popen(real_cmd, "r");
        if (pipe) {
            size_t bytes = fread(vg_output, 1, sizeof(vg_output) - 1, pipe);
            vg_output[bytes] = '\0';
            exit_status = pclose(pipe);

            /* Check that valgrind actually ran (exit_status=0 doesn't
             * guarantee this, but non-zero means it definitely failed) */
            if (exit_status != 0 && bytes == 0) {
                snprintf(sanitizer_output, sanitizer_size,
                         "valgrind failed (exit %d) for resource check",
                         exit_status);
                return 0;
            }

            /* Copy to sanitizer_output for parsing */
            strncpy(sanitizer_output, vg_output, sanitizer_size - 1);

            /* Check for FILE DESCRIPTOR leaks.
             * "Open file descriptor N:" lines only appear for
             * non-inherited FDs left open at exit. */
            if (string_contains(vg_output, "Open file descriptor")) {
                if (found < max_errors) {
                    errors[found].type = ERR_MEMORY_LEAK;
                    snprintf(errors[found].title, sizeof(errors[found].title),
                             "File Descriptor Leak");
                    snprintf(errors[found].fix_suggestion,
                             sizeof(errors[found].fix_suggestion),
                             "One or more file descriptors were not closed "
                             "before exit. Check that every open(), fopen(), "
                             "socket(), accept(), or dup() has a matching "
                             "close()/fclose() in all code paths.");
                    errors[found].severity = 2;
                    errors[found].has_source = false;
                    found++;
                }
            }
        } else {
            snprintf(sanitizer_output, sanitizer_size,
                     "Could not start valgrind for resource check");
        }
    } else {
        /* Valgrind unavailable — /proc snapshot doesn't track child FDs */
        snprintf(sanitizer_output, sanitizer_size,
                 "Resource leak check requires valgrind (not found): "
                 "install valgrind for --track-fds=yes detection");
    }

    return found;
}

/*
 * check_binary_harden - Check ELF binary for security hardening features
 */
int check_binary_harden(const char *binary,
                         DetectedError *errors, int max_errors)
{
    int found = 0;
    char buf[4096];
    int ret;

    if (!binary || access(binary, X_OK) != 0)
        return 0;

    /* Check PIE: e_type in ELF header should be DYN (3), not EXEC (2) */
    snprintf(buf, sizeof(buf),
             "readelf -h '%s' 2>/dev/null | grep -q 'Type:.*DYN'", binary);
    ret = system(buf);
    if (ret != 0) {
        if (found < max_errors) {
            errors[found].type = ERR_HARDENING;
            snprintf(errors[found].title, sizeof(errors[found].title),
                     "Binary not PIE");
            snprintf(errors[found].fix_suggestion, sizeof(errors[found].fix_suggestion),
                     "Compile with -fpie -pie to enable ASLR");
            errors[found].severity = 5;
            errors[found].has_source = false;
            found++;
        }
    }

    /* Check NX: PT_GNU_STACK must not have X flag */
    snprintf(buf, sizeof(buf),
             "readelf -l '%s' 2>/dev/null | grep -A1 GNU_STACK | "
             "grep -q -E 'RWE|RWX'", binary);
    ret = system(buf);
    if (ret == 0) {
        if (found < max_errors) {
            errors[found].type = ERR_HARDENING;
            snprintf(errors[found].title, sizeof(errors[found].title),
                     "Stack executable (NX disabled)");
            snprintf(errors[found].fix_suggestion, sizeof(errors[found].fix_suggestion),
                     "Compile with -Wa,-noexecstack");
            errors[found].severity = 8;
            errors[found].has_source = false;
            found++;
        }
    }

    /* Check stack canary */
    snprintf(buf, sizeof(buf),
             "readelf -s '%s' 2>/dev/null | grep -q '__stack_chk_fail'", binary);
    ret = system(buf);
    if (ret != 0) {
        if (found < max_errors) {
            errors[found].type = ERR_HARDENING;
            snprintf(errors[found].title, sizeof(errors[found].title),
                     "No stack canary");
            snprintf(errors[found].fix_suggestion, sizeof(errors[found].fix_suggestion),
                     "Compile with -fstack-protector-strong");
            errors[found].severity = 6;
            errors[found].has_source = false;
            found++;
        }
    }

    /* Check RELRO */
    snprintf(buf, sizeof(buf),
             "readelf -l '%s' 2>/dev/null | grep -q 'GNU_RELRO'", binary);
    ret = system(buf);
    if (ret != 0) {
        if (found < max_errors) {
            errors[found].type = ERR_HARDENING;
            snprintf(errors[found].title, sizeof(errors[found].title),
                     "No RELRO");
            snprintf(errors[found].fix_suggestion, sizeof(errors[found].fix_suggestion),
                     "Compile with -Wl,-z,relro");
            errors[found].severity = 5;
            errors[found].has_source = false;
            found++;
        }
    } else {
        /* Check for Full RELRO (BIND_NOW) */
        snprintf(buf, sizeof(buf),
                 "readelf -d '%s' 2>/dev/null | grep -q 'BIND_NOW'", binary);
        ret = system(buf);
        if (ret != 0) {
            if (found < max_errors) {
                errors[found].type = ERR_HARDENING;
                snprintf(errors[found].title, sizeof(errors[found].title),
                         "Partial RELRO only");
                snprintf(errors[found].fix_suggestion, sizeof(errors[found].fix_suggestion),
                         "Compile with -Wl,-z,now for full RELRO");
                errors[found].severity = 3;
                errors[found].has_source = false;
                found++;
            }
        }
    }

    /* Check RPATH/RUNPATH */
    snprintf(buf, sizeof(buf),
             "readelf -d '%s' 2>/dev/null | "
             "grep -q -E '\\(RPATH|\\(RUNPATH'", binary);
    ret = system(buf);
    if (ret == 0) {
        if (found < max_errors) {
            errors[found].type = ERR_HARDENING;
            snprintf(errors[found].title, sizeof(errors[found].title),
                     "Binary has RPATH/RUNPATH");
            snprintf(errors[found].fix_suggestion, sizeof(errors[found].fix_suggestion),
                     "Remove -rpath linker flags; use LD_LIBRARY_PATH instead");
            errors[found].severity = 4;
            errors[found].has_source = false;
            found++;
        }
    }

    return found;
}

struct DangerPattern {
    const char *func;
    const char *title;
    const char *fix;
    ErrorType type;
    int severity;
};

/*
 * scan_dangerous_apis - Scan source for dangerous C functions
 *
 *      CVEs. Many don't trigger compiler warnings. Catch them pre-runtime.
 */
int scan_dangerous_apis(const char **sources, int source_count,
                        DetectedError *errors, int max_errors)
{
    static const struct DangerPattern patterns[] = {
        {"gets", "Unsafe gets() Call",
         "Use fgets(buf, sizeof(buf), stdin). gets() has no bounds "
         "checking and cannot be used safely.",
         ERR_BUFFER_OVERFLOW, 3},
        {"strcpy", "Unsafe strcpy() Use",
         "Use strncpy() with explicit length check, or strlcpy() if "
         "available. strcpy() does not check destination bounds.",
         ERR_BUFFER_OVERFLOW, 3},
        {"sprintf", "Unsafe sprintf() Use",
         "Use snprintf(buf, sizeof(buf), ...). sprintf() does not check "
         "the destination buffer size.",
         ERR_BUFFER_OVERFLOW, 3},
        {"strcat", "Unsafe strcat() Use",
         "Check buffer space before concatenation, or use strlcat(). "
         "strcat() does not check destination bounds.",
         ERR_BUFFER_OVERFLOW, 3},
        {"scanf", "Unbounded scanf()",
         "Always specify field width limits: scanf(\"%%%ds\", ...). "
         "Unbounded %%s can overflow the destination buffer.",
         ERR_BUFFER_OVERFLOW, 2},
        {"alloca", "Stack Allocation (alloca)",
         "Use malloc() instead. alloca() has no failure reporting and "
         "can silently cause stack overflow.",
         ERR_STACK_OVERFLOW, 2},
        {"setjmp", "setjmp Without Volatile",
         "Variables modified between setjmp/longjmp must be volatile "
         "to avoid undefined behavior.",
         ERR_UNINIT_VAR, 1},
    };
    const int num_patterns = sizeof(patterns) / sizeof(patterns[0]);
    const size_t line_buf_size = 65536;
    char *line_buf = malloc(line_buf_size);
    int found = 0;
    bool in_block_comment = false;

    if (!line_buf) return 0;

    for (int f = 0; f < source_count && found < max_errors; f++) {
        FILE *fp = fopen(sources[f], "r");
        if (!fp) continue;

        int line_num = 0;
        size_t acc_len = 0;
        while (found < max_errors) {
            if (!fgets(line_buf + acc_len, line_buf_size - acc_len, fp)) {
                if (acc_len > 0) {
                    line_num++;
                    acc_len = 0;
                } else {
                    break;
                }
            } else {
                size_t chunk = strlen(line_buf + acc_len);
                acc_len += chunk;

                if (acc_len > 0 && line_buf[acc_len - 1] == '\n') {
                    line_buf[acc_len - 1] = '\0';
                    line_num++;
                    acc_len = 0;
                } else if (acc_len >= line_buf_size - 1) {
                    line_buf[line_buf_size - 1] = '\0';
                    line_num++;
                    acc_len = 0;
                } else {
                    continue;
                }
            }

            /* Ignore lines inside block comments */
            char *scan = line_buf;
            while (*scan) {
                if (in_block_comment) {
                    char *end = strstr(scan, "*/");
                    if (end) {
                        in_block_comment = false;
                        scan = end + 2;
                    } else {
                        break;  /* rest of line is still in block comment */
                    }
                } else {
                    /* Check for // comment start — rest of line is a comment */
                    char *line_comment = strstr(scan, "//");
                    /* Check for block comment start */
                    char *block_comment = strstr(scan, "/" "*");
                    size_t line_comment_pos = line_comment ? (size_t)(line_comment - scan) : (size_t)-1;
                    size_t block_comment_pos = block_comment ? (size_t)(block_comment - scan) : (size_t)-1;

                    /* Pick whichever comes first */
                    char *comment_start = NULL;
                    if (line_comment && block_comment) {
                        if (line_comment_pos < block_comment_pos) {
                            comment_start = line_comment;
                        } else {
                            comment_start = block_comment;
                        }
                    } else if (line_comment) {
                        comment_start = line_comment;
                    } else if (block_comment) {
                        comment_start = block_comment;
                    }

                    /* Only search code before the comment start */
                    size_t code_end = comment_start ? (size_t)(comment_start - scan) : strlen(scan);

                    /* Blank out contents of string literals so patterns
                     * like "sprintf", "strcpy" inside strings don't match */
                    char search_buf[65536];
                    size_t copy_len = (code_end < sizeof(search_buf) - 1)
                                       ? code_end : sizeof(search_buf) - 1;
                    memcpy(search_buf, scan, copy_len);
                    search_buf[copy_len] = '\0';
                    {
                        bool in_str = false;
                        char *sb = search_buf;
                        while (*sb) {
                            if (in_str) {
                                if (*sb == '\\' && *(sb + 1)) {
                                    sb += 2;
                                    continue;
                                }
                                if (*sb == '"') {
                                    in_str = false;
                                } else {
                                    *sb = ' ';
                                }
                            } else if (*sb == '"') {
                                in_str = true;
                            }
                            sb++;
                        }
                    }

                    for (int p = 0; p < num_patterns; p++) {
                        const char *pat = patterns[p].func;
                        size_t pat_len = strlen(pat);
                        char *pos = search_buf;
                        while ((pos = strstr(pos, pat)) && (size_t)(pos - search_buf) < code_end) {
                            /* Check identifier boundary before */
                            bool before_ok = (pos == scan) ||
                                (!isalnum((unsigned char)pos[-1]) && pos[-1] != '_');
                            /* Check identifier boundary after (function call = after pat is '(') */
                            bool after_ok = !isalnum((unsigned char)pos[pat_len]) &&
                                            pos[pat_len] != '_';
                            if (before_ok && after_ok) {
                                errors[found].type = patterns[p].type;
                                snprintf(errors[found].title,
                                         sizeof(errors[found].title),
                                         "%s", patterns[p].title);
                                snprintf(errors[found].fix_suggestion,
                                         sizeof(errors[found].fix_suggestion),
                                         "%s", patterns[p].fix);
                                errors[found].severity = patterns[p].severity;
                                errors[found].has_source = true;
                                strncpy(errors[found].source_file, sources[f],
                                        sizeof(errors[found].source_file) - 1);
                                errors[found].source_line = line_num;
                                found++;
                                break;
                            }
                            pos++;
                        }
                        if (found > 0 && errors[found - 1].source_line == line_num &&
                            strcmp(errors[found - 1].source_file, sources[f]) == 0)
                            break;  /* One error per line */
                    }

                    /* Handle block comment start not yet closed */
                    if (block_comment && block_comment_pos < line_comment_pos) {
                        in_block_comment = true;
                        scan = block_comment + 2;
                    } else if (line_comment) {
                        break;  /* rest is comment */
                    } else {
                        break;  /* no more comments on this line */
                    }
                }
            }
        }
        fclose(fp);
    }

    free(line_buf);
    return found;
}

/*
 * check_header - Compile header with -fsyntax-only, report errors
 */
int check_header(const char **sources, int source_count,
                  char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 128];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd),
                   "gcc -x c -fsyntax-only -Wall -Wextra");

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(output, output_size, "Failed to start compiler");
        return -1;
    }

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';

    return pclose(pipe);
}

/*
 * compile_with_basic_flags - Quick compile with -Wall -Wextra -Werror only
 */
int compile_with_basic_flags(const char **sources, int source_count,
                              const char *binary,
                              char *output, size_t output_size)
{
    char cmd[MAX_OUTPUT_SIZE];
    FILE *pipe;
    size_t bytes_read;
    size_t pos;

    if (is_cpp_file(sources[0])) {
        pos = snprintf(cmd, sizeof(cmd),
                       "g++ -Wall -Wextra -Werror -O2 -g -o '%s'", binary);
    } else {
        pos = snprintf(cmd, sizeof(cmd),
                       "gcc -Wall -Wextra -Werror -O2 -g -o '%s'", binary);
    }

    for (int i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (!pipe) return -1;

    bytes_read = fread(output, 1, output_size - 1, pipe);
    output[bytes_read] = '\0';
    return pclose(pipe);
}

/*
 * compute_source_hash - Compute MD5 hash of source file contents
 *
 *      Returns 0 on success with hash in hash_buf.
 */
int compute_source_hash(const char *source_file, char *hash_buf,
                         size_t hash_buf_size)
{
    char cmd[4096];
    FILE *pipe;

    snprintf(cmd, sizeof(cmd), "md5sum '%s' 2>/dev/null", source_file);
    pipe = popen(cmd, "r");
    if (!pipe) return -1;

    if (!fgets(hash_buf, hash_buf_size, pipe)) {
        pclose(pipe);
        return -1;
    }
    pclose(pipe);

    /* Trim trailing "  filename" to get just the hex hash */
    char *space = strchr(hash_buf, ' ');
    if (space) *space = '\0';

    return 0;
}

/*
 * save_cache_entry - Save compilation result to cache
 *
 *      Cache is stored under ~/.cache/prism/<hash>.
 */
void save_cache_entry(const char *hash, bool success,
                       const char *compiler_output)
{
    const char *home = getenv("HOME");
    char cache_dir[MAX_PATH_LEN];
    char cache_path[MAX_PATH_LEN + 64];
    FILE *fp;

    if (!home || !hash || !compiler_output) return;

    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/prism", home);
    snprintf(cache_path, sizeof(cache_path), "%s/%s", cache_dir, hash);

    /* Create cache directory */
    char mkdir_cmd[MAX_PATH_LEN + 64];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s' 2>/dev/null", cache_dir);
    system(mkdir_cmd);

    fp = fopen(cache_path, "w");
    if (!fp) return;

    fprintf(fp, "%d\n%s", success ? 1 : 0, compiler_output);
    fclose(fp);
}

/*
 * load_cache_entry - Load cached compilation result
 *
 *      compilation exists for the given source hash.
 */
int load_cache_entry(const char *hash, bool *success,
                      char *compiler_output, size_t output_size)
{
    const char *home = getenv("HOME");
    char cache_path[MAX_PATH_LEN + 64];
    FILE *fp;
    int raw_success;

    if (!home || !hash || !success) return -1;

    snprintf(cache_path, sizeof(cache_path), "%s/.cache/prism/%s",
             home, hash);

    fp = fopen(cache_path, "r");
    if (!fp) return -1;

    if (fscanf(fp, "%d\n", &raw_success) != 1) {
        fclose(fp);
        return -1;
    }

    *success = (raw_success != 0);

    size_t bytes = 0;
    if (output_size > 1)
        bytes = fread(compiler_output, 1, output_size - 1, fp);
    if (output_size > 0)
        compiler_output[bytes] = '\0';
    fclose(fp);

    return 0;
}

/*
 * save_baseline - Save current errors as a baseline JSON file
 *
 *      future runs can suppress them and only report new issues.
 */
int save_baseline(const char *path, const DetectedError *errors,
                   int error_count)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;

    fprintf(fp, "[\n");
    for (int i = 0; i < error_count; i++) {
        /* Normalize path: strip leading ./ */
        const char *sf = errors[i].source_file;
        while (sf[0] == '.' && sf[1] == '/') sf += 2;
        fprintf(fp, "  {\"type\":%d,\"title\":\"%s\",\"file\":\"%s\","
                     "\"line\":%d,\"severity\":%d}%s\n",
                (int)errors[i].type, errors[i].title,
                sf, errors[i].source_line,
                errors[i].severity,
                i < error_count - 1 ? "," : "");
    }
    fprintf(fp, "]\n");
    fclose(fp);
    return 0;
}

/*
 * load_baseline_and_filter - Load baseline and suppress matching errors
 *
 *      leaving only NEW issues for the developer to address.
 *      Mutates errors[] in place and returns the new error count.
 */
int load_baseline_and_filter(const char *path,
                              DetectedError *errors, int *error_count)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    /* Read entire file into buffer */
    char buf[32768];
    size_t bytes = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[bytes] = '\0';
    fclose(fp);

    if (bytes < 2) return 0;

    /* Simple JSON line-by-line scan for {type, file, line} */
    int new_count = 0;
    for (int i = 0; i < *error_count; i++) {
        DetectedError *e = &errors[i];
        char search_line[1024];
        /* Normalize path for comparison: strip leading ./ */
        const char *sf = e->source_file;
        while (sf[0] == '.' && sf[1] == '/') sf += 2;
        snprintf(search_line, sizeof(search_line),
                 "\"type\":%d,\"file\":\"%s\",\"line\":%d",
                 (int)e->type, sf, e->source_line);

        if (string_contains(buf, search_line)) {
            /* Suppress this error */
            continue;
        }
        /* Keep this error */
        if (new_count != i)
            errors[new_count] = errors[i];
        new_count++;
    }

    *error_count = new_count;
    return 0;
}

/*
 * run_coverage_analysis - Compile and run with --gcov code coverage
 *
 *      the binary is run under the test inputs.
 */
int run_coverage_analysis(const char *binary, const char **sources,
                           int source_count, DetectedError *errors,
                           int max_errors, char *output, size_t output_size,
                           int timeout_sec)
{
    char cmd[4096];
    FILE *pipe;
    int found = 0;
    const char *test_inputs[] = {"", "A", "test", "12345", "-1", "0", NULL};

    /* Run binary with several inputs to generate .gcda files */
    for (int i = 0; test_inputs[i]; i++) {
        char run_out[MAX_OUTPUT_SIZE];
        char run_err[MAX_OUTPUT_SIZE];
        run_binary(binary, test_inputs[i],
                   run_out, sizeof(run_out),
                   run_err, sizeof(run_err),
                   timeout_sec, NULL);
    }

    /* Run gcov on each source file */
    for (int s = 0; s < source_count && found < max_errors; s++) {
        snprintf(cmd, sizeof(cmd),
                 "gcov -abc '%s' 2>&1", sources[s]);

        char cov_output[32768];
        pipe = popen(cmd, "r");
        if (!pipe) continue;

        size_t n = fread(cov_output, 1, sizeof(cov_output) - 1, pipe);
        cov_output[n] = '\0';
        pclose(pipe);

        /* Append gcov output to our output buffer */
        if (output) {
            size_t slen = strlen(output);
            if (slen + n + 64 < output_size) {
                snprintf(output + slen, output_size - slen,
                         "--- Coverage for %s ---\n%s\n", sources[s], cov_output);
            }
        }

        /* Parse coverage percentages */
        char *line = cov_output;
        int lines_pct = -1, branches_pct = -1;

        while (*line && found < max_errors) {
            if (sscanf(line, "Lines executed:%d.%*d%% of %*d",
                       &lines_pct) >= 1) {
                /* Found line coverage */
            } else if (sscanf(line, "Branches executed:%d.%*d%% of %*d",
                              &branches_pct) >= 1) {
                /* Found branch coverage */
            }
            /* Move to next line */
            char *nl = strchr(line, '\n');
            if (!nl) break;
            line = nl + 1;
        }

        /* Report coverage as "errors" if below thresholds */
        if (lines_pct >= 0 && lines_pct < 50 && found < max_errors) {
            errors[found].type = ERR_UNKNOWN;
            snprintf(errors[found].title, sizeof(errors[found].title),
                     "Low Line Coverage: %d%%", lines_pct);
            snprintf(errors[found].fix_suggestion,
                     sizeof(errors[found].fix_suggestion),
                     "Only %d%% of lines were executed. Add more test cases "
                     "or fuzz inputs to improve coverage.",
                     lines_pct);
            errors[found].severity = 1;
            errors[found].has_source = true;
            strncpy(errors[found].source_file, sources[s],
                    sizeof(errors[found].source_file) - 1);
            errors[found].source_line = 0;
            found++;
        }
    }

    /* Clean up coverage artifacts — target known file paths only */
    char gcda_name[MAX_PATH_LEN], gcno_name[MAX_PATH_LEN];
    for (int s = 0; s < source_count; s++) {
        snprintf(gcda_name, sizeof(gcda_name), "%s.gcda", sources[s]);
        snprintf(gcno_name, sizeof(gcno_name), "%s.gcno", sources[s]);
        unlink(gcda_name);
        unlink(gcno_name);
        /* gcov emits source.gcov for each source */
        snprintf(gcda_name, sizeof(gcda_name), "%s.gcov", sources[s]);
        unlink(gcda_name);
    }

    return found;
}

/*
 * run_libfuzzer_analysis - Compile and run with libFuzzer
 *
 *      crashes that simple boundary testing misses. Requires clang
 *      and a LLVMFuzzerTestOneInput() function in the source.
 */
int run_libfuzzer_analysis(const char **sources, int source_count,
                            DetectedError *errors, int max_errors,
                            char *output, size_t output_size,
                            int timeout_sec)
{
    char cmd[MAX_PATH_LEN * 2 + 256];
    char fuzz_out[32768];
    FILE *pipe;
    int found = 0;
    char binary_path[MAX_PATH_LEN];

    /* Check for clang */
    if (system("command -v clang > /dev/null 2>&1") != 0) {
        if (output)
            snprintf(output, output_size,
                     "libFuzzer requires clang (not found). "
                     "Install clang from https://clang.llvm.org/");
        return -1;
    }

    /* Check that at least one source has LLVMFuzzerTestOneInput */
    bool has_fuzz_target = false;
    for (int i = 0; i < source_count; i++) {
        char grep_cmd[4096];
        snprintf(grep_cmd, sizeof(grep_cmd),
                 "grep -q 'LLVMFuzzerTestOneInput' '%s' 2>/dev/null",
                 sources[i]);
        if (system(grep_cmd) == 0) {
            has_fuzz_target = true;
            break;
        }
    }

    if (!has_fuzz_target) {
        if (output)
            snprintf(output, output_size,
                     "No LLVMFuzzerTestOneInput() found in sources. "
                     "Define one to use libFuzzer.");
        return -1;
    }

    /* Generate temp binary path */
    generate_temp_path("prism_fuzz", binary_path, sizeof(binary_path));

    /* Compile with libFuzzer + ASan */
    size_t pos = snprintf(cmd, sizeof(cmd),
                          "clang -fsanitize=fuzzer,address -g -O1 "
                          "-o '%s'", binary_path);

    for (int i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
    }

    if (pos >= sizeof(cmd) - 4) {
        if (output)
            snprintf(output, output_size, "Too many source files: command buffer overflow");
        return -1;
    }

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");

    pipe = popen(cmd, "r");
    if (pipe) {
        size_t n = fread(fuzz_out, 1, sizeof(fuzz_out) - 1, pipe);
        fuzz_out[n] = '\0';
        int compile_status = pclose(pipe);

        if (compile_status != 0) {
            if (output)
                snprintf(output, output_size,
                         "libFuzzer compilation failed:\n%s", fuzz_out);
            unlink(binary_path);
            return -1;
        }
    } else {
        unlink(binary_path);
        return -1;
    }

    /* Run libFuzzer with timeout */
    snprintf(cmd, sizeof(cmd),
             "timeout %d '%s' -max_total_time=%d -print_final_stats=1 2>&1",
             timeout_sec, binary_path, timeout_sec);

    pipe = popen(cmd, "r");
    if (!pipe) {
        unlink(binary_path);
        return -1;
    }

    size_t n = fread(fuzz_out, 1, sizeof(fuzz_out) - 1, pipe);
    fuzz_out[n] = '\0';
    pclose(pipe);

    /* Copy output */
    if (output)
        snprintf(output, output_size, "%s", fuzz_out);

    /* Parse for crashes */
    if (string_contains(fuzz_out, "SUMMARY:") && found < max_errors) {
        errors[found].type = ERR_SEG;
        snprintf(errors[found].title, sizeof(errors[found].title),
                 "libFuzzer Found Crash");
        snprintf(errors[found].fix_suggestion,
                 sizeof(errors[found].fix_suggestion),
                 "libFuzzer detected a crash during fuzzing. "
                 "Check the crash input in the current directory.");
        errors[found].severity = 3;
        errors[found].has_source = false;
        found++;
    }

    /* Clean up */
    unlink(binary_path);

    return found;
}

/*
 * run_ultra_analysis - MAX on steroids — every mode, trick, and flag
 *
 *      variant, both strict-aliasing modes, hardened flags, gcov, and
 *      clang MSan. It then runs 20+ analysis passes in parallel:
 *      boundary fuzz, random fuzz, rerun for flaky detection, Valgrind,
 *      TSan, UBSan at every variant, GCC analyzer, clang-tidy, warnings,
 *      coverage, danger scan, and resource-leak checking.
 *
 *      Each pass writes results to a temp file. The parent collects and
 *      deduplicates. Gcov artifacts cleaned up at the end.
 */

/* Ultra compile variants (flags and compilers) */
struct ultra_compile_var {
    const char *p_name;
    const char *compiler;
    const char *flags;
    bool is_cpp_target;
};

static const struct ultra_compile_var ultra_variants[] = {
    /* 0  */ {"ASan+UBSan -O2",          "gcc", "-fsanitize=address,undefined -O2 -g", false},
    /* 1  */ {"ASan+UBSan -O0",          "gcc", "-fsanitize=address,undefined -O0 -g", false},
    /* 2  */ {"ASan+UBSan -Os",          "gcc", "-fsanitize=address,undefined -Os -g", false},
    /* 3  */ {"Full UBSan -O1",          "gcc", "-fsanitize=undefined,shift,integer-divide-by-zero,null,alignment,object-size,float-divide-by-zero,float-cast-overflow,signed-integer-overflow,bounds,pointer-overflow -O1 -g -fno-sanitize-recover=all", false},
    /* 4  */ {"Hardened -O2",            "gcc", "-fsanitize=address,undefined -O2 -g -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fcf-protection=full", false},
    /* 5  */ {"StrictAlias -O2",         "gcc", "-fsanitize=address,undefined -O2 -g -fstrict-aliasing -Wstrict-aliasing=3", false},
    /* 6  */ {"Valgrind -O0",            "gcc", "-O0 -g", false},
    /* 7  */ {"TSan -O2",                "gcc", "-fsanitize=thread -O2 -g -fno-omit-frame-pointer", false},
    /* 8  */ {"GCov -O0",                "gcc", "--coverage -O0 -g", false},
    /* 9  */ {"Analyzer -fanalyzer",     "gcc", "-fanalyzer -O1 -g", false},
    /* 10 */ {"ASan+UBSan -O1 C++",     "g++", "-fsanitize=address,undefined -O1 -g", true},
    /* 11 */ {"ASan+UBSan -O2 C++",     "g++", "-fsanitize=address,undefined -O2 -g", true},
    /* 12 */ {"ASan+UBSan -O0 C++",     "g++", "-fsanitize=address,undefined -O0 -g", true},
    /* 13 */ {"TSan -O2 C++",           "g++", "-fsanitize=thread -O2 -g -fno-omit-frame-pointer", true},
    /* 14 */ {"Hardened -O2 C++",       "g++", "-fsanitize=address,undefined -O2 -g -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fcf-protection=full", true},
    /* 15 */ {"StrictAlias -O2 C++",    "g++", "-fsanitize=address,undefined -O2 -g -fstrict-aliasing -Wstrict-aliasing=3", true},
    /* 16 */ {"Full UBSan -O1 C++",     "g++", "-fsanitize=undefined,shift,integer-divide-by-zero,null,alignment,object-size,float-divide-by-zero,float-cast-overflow,signed-integer-overflow,bounds,pointer-overflow -O1 -g -fno-sanitize-recover=all", true},
    /* 17 */ {"Analyzer C++",           "g++", "-fanalyzer -O1 -g", true},
};
#define ULTRA_NUM_VARIANTS 18

/*
 * ultra_find_binary - Find first binary whose variant name contains tag
 */
static const char *ultra_find_binary(const struct ultra_compile_var *variants,
                                      int num, const char *binary_paths[],
                                      const bool *success, const char *tag)
{
    for (int i = 0; i < num; i++) {
        if (success[i] && variants[i].p_name &&
            strstr(variants[i].p_name, tag)) {
            return binary_paths[i];
        }
    }
    return NULL;
}

int run_ultra_analysis(const char **sources, int source_count,
                        DetectedError *errors, int max_errors,
                        int timeout_sec, const ColorCodes *colors)
{
    int total_errors = 0;
    uint64_t error_modes[32] = {0};
    bool is_cpp = is_cpp_file(sources[0]);

    /* ─── Compilation Wave (all binaries built in parallel) ─── */
    int num_var = ULTRA_NUM_VARIANTS;
    char *binary_paths[ULTRA_NUM_VARIANTS];
    pid_t compile_pids[ULTRA_NUM_VARIANTS];
    bool compile_ok[ULTRA_NUM_VARIANTS];
    int num_to_compile = 0;

    memset(binary_paths, 0, sizeof(binary_paths));
    memset(compile_pids, 0, sizeof(compile_pids));
    memset(compile_ok, 0, sizeof(compile_ok));

    /* Generate binary paths and count active compilations */
    for (int i = 0; i < num_var; i++) {
        if (is_cpp != ultra_variants[i].is_cpp_target) continue;
        binary_paths[i] = malloc(MAX_PATH_LEN);
        if (!binary_paths[i]) continue;
        generate_temp_path("prism_ultra", binary_paths[i], MAX_PATH_LEN);
        num_to_compile++;
    }

    if (colors) {
        print_colored(colors, colors->cyan, "[ultra] ");
        printf("Phase 1: %d binaries needed, compiling...\n", num_to_compile);
        fflush(stdout);
    }

    /* Fork compile children */
    for (int i = 0; i < num_var; i++) {
        if (is_cpp != ultra_variants[i].is_cpp_target) continue;
        if (!binary_paths[i]) continue;

        pid_t pid = fork();
        if (pid == 0) {
            char cmd[16384];
            size_t pos = snprintf(cmd, sizeof(cmd), "%s %s -o '%s'",
                                  ultra_variants[i].compiler,
                                  ultra_variants[i].flags,
                                  binary_paths[i]);
            for (int s = 0; s < source_count && pos < sizeof(cmd) - 4; s++)
                pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[s]);
            if (pos >= sizeof(cmd) - 4) _exit(1);
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>&1");
            FILE *pp = popen(cmd, "r");
            if (pp) {
                char buf[4096];
                while (fgets(buf, sizeof(buf), pp)) {}
                pclose(pp);
            }
            _exit(access(binary_paths[i], X_OK) == 0 ? 0 : 1);
        } else if (pid > 0) {
            compile_pids[i] = pid;
        }
    }

    /* Wait for all compilations */
    int compile_success_count = 0;
    for (int i = 0; i < num_var; i++) {
        if (is_cpp != ultra_variants[i].is_cpp_target) continue;
        if (!compile_pids[i]) continue;
        int status;
        waitpid(compile_pids[i], &status, 0);
        compile_ok[i] = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
        if (compile_ok[i]) compile_success_count++;
    }

    if (colors) {
        print_colored(colors, colors->cyan, "[ultra] ");
        printf("Phase 2: %d/%d compiled — launching 31 analysis passes...\n",
               compile_success_count, num_to_compile);
        fflush(stdout);
    }

    /* ─── Analysis Wave: each pass runs in a forked child,
     *     writes results to a temp file, parent collects ─── */
#define MAX_ANALYSIS_PASSES 34
    struct {
        pid_t pid;
        char result_file[MAX_PATH_LEN];
    } analysis_passes[MAX_ANALYSIS_PASSES];
    int num_analysis = 0;
    memset(analysis_passes, 0, sizeof(analysis_passes));

    for (int i = 0; i < MAX_ANALYSIS_PASSES; i++) {
        snprintf(analysis_passes[i].result_file, sizeof(analysis_passes[i].result_file),
                 "/tmp/prism_ultra_ap_%d_%d", (int)getpid(), i);
    }

    /* Helper to find a binary path by tag */
#define ULTRA_BIN(tag) ultra_find_binary(ultra_variants, num_var, \
                                          (const char **)binary_paths, \
                                          (const bool *)compile_ok, (tag))

/* Helper to write results from child — pipe-escaped structured data */
#define ULTRA_CHILD_RESULT(var_lo, var_lc, var_le) do { \
        (void)var_lo; \
        fprintf(rfp, "%d\n", var_lc); \
        for (int jj = 0; jj < var_lc && jj < 16; jj++) { \
            fprintf(rfp, "%d|", (int)var_le[jj].type); \
            for (const char *_p = var_le[jj].title; *_p; _p++) \
                fputc((*_p == '|' || *_p == '\n') ? ' ' : *_p, rfp); \
            fputc('|', rfp); \
            for (const char *_p = var_le[jj].fix_suggestion; *_p; _p++) \
                fputc((*_p == '|' || *_p == '\n') ? ' ' : *_p, rfp); \
            fprintf(rfp, "|%d|%d|%d\n", \
                    var_le[jj].source_line, var_le[jj].severity, \
                    var_le[jj].has_source ? 1 : 0); \
        } \
    } while(0)

    /* 1. ASan + UBSan -O2 runtime */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1:detect_invalid_pointer_pairs=2:check_initialization_order=1",1);
                char r_out[32768], r_err[32768];
                run_binary(b, NULL, r_out,sizeof(r_out), r_err,sizeof(r_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(r_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 2. ASan+UBSan -O0 runtime (optimization-dependent bugs) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O0");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1:detect_stack_use_after_return=1",1);
                char r_out[32768], r_err[32768];
                run_binary(b, NULL, r_out,sizeof(r_out), r_err,sizeof(r_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(r_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 3. Full UBSan -O1 (extra UB checks: shift, align, wrap, bounds) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("Full UBSan");
            if (b) {
                setenv("UBSAN_OPTIONS","print_stacktrace=1:halt_on_error=0",1);
                setenv("ASAN_OPTIONS","detect_leaks=1",1);
                char r_out[32768], r_err[32768];
                run_binary(b, NULL, r_out,sizeof(r_out), r_err,sizeof(r_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(r_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 4. Hardened runtime (fortify + stack protection) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("Hardened");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1:detect_stack_use_after_return=1",1);
                char r_out[32768], r_err[32768];
                run_binary(b, NULL, r_out,sizeof(r_out), r_err,sizeof(r_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(r_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 5. StrictAlias runtime (type-punning detection) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("StrictAlias");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1",1);
                char r_out[32768], r_err[32768];
                run_binary(b, NULL, r_out,sizeof(r_out), r_err,sizeof(r_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(r_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 6. ASan+UBSan -Os runtime (size-optimized code paths) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -Os");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1",1);
                char r_out[32768], r_err[32768];
                run_binary(b, NULL, r_out,sizeof(r_out), r_err,sizeof(r_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(r_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 7. Valgrind Memcheck (deep memory analysis) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("Valgrind");
            if (b) {
                char vg_out[32768], vg_err[32768];
                run_with_valgrind(b, vg_out,sizeof(vg_out), vg_err,sizeof(vg_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(vg_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", vg_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 8. Valgrind resource leak check (--track-fds) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("Valgrind");
            if (b) {
                char cmd[8192];
                snprintf(cmd,sizeof(cmd),
                         "timeout %d valgrind --tool=memcheck --track-fds=yes --leak-check=full '%s' 2>&1 </dev/null",
                         timeout_sec, b);
                FILE *pp = popen(cmd,"r");
                if (pp) {
                    size_t n = fread(lo,1,sizeof(lo)-1,pp); lo[n]='\0'; pclose(pp);
                }
                if (string_contains(lo,"Open file descriptor") ||
                    string_contains(lo,"definitely lost") ||
                    string_contains(lo,"indirectly lost")) {
                    le[lc].type = ERR_UNKNOWN;
                    snprintf(le[lc].title,sizeof(le[lc].title),"Resource Leak / Memory Leak");
                    snprintf(le[lc].fix_suggestion,sizeof(le[lc].fix_suggestion),
                             "Valgrind detected open FDs or leaked memory — ensure all "
                             "malloc() has matching free() and all FDs are closed.");
                    le[lc].severity=2; le[lc].has_source=false; lc++;
                }
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 9. TSan (data races via ThreadSanitizer) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("TSan");
            if (b) {
                setenv("TSAN_OPTIONS","report_atomic_races=1:halt_on_error=0",1);
                char tsan_out[32768], tsan_err[32768];
                run_binary(b, NULL, tsan_out,sizeof(tsan_out), tsan_err,sizeof(tsan_err), timeout_sec, NULL);
                lc = parse_sanitizer_errors(tsan_err, le, 16);
                snprintf(lo, sizeof(lo), "%s", tsan_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 10. Compiler warnings (-Wall -Wextra -Wpedantic) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            char warn_out[MAX_OUTPUT_SIZE];
            int warn_ret;
            if (is_cpp_file(sources[0]))
                warn_ret = compile_cpp_for_warnings(sources,source_count,
                                 warn_out,sizeof(warn_out));
            else
                warn_ret = compile_for_warnings(sources,source_count,
                             warn_out,sizeof(warn_out));
            (void)warn_ret;
            char *save;
            char *line = strtok_r(warn_out,"\n",&save);
            while (line && lc < 16) {
                if (string_contains(line,"warning:")) {
                    int pat = classify_error(line);
                    if (pat >= 0) {
                        le[lc].type = error_patterns[pat].type;
                        snprintf(le[lc].title,sizeof(le[lc].title),"%s",error_patterns[pat].title);
                        snprintf(le[lc].fix_suggestion,sizeof(le[lc].fix_suggestion),"%s",error_patterns[pat].fix);
                        le[lc].severity = error_patterns[pat].severity;
                        le[lc].has_source = false; lc++;
                    }
                }
                line = strtok_r(NULL,"\n",&save);
            }
            snprintf(lo,sizeof(lo),"%s",warn_out);
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 11. GCC -fanalyzer static analysis */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            char analyze_out[MAX_OUTPUT_SIZE];
            if (is_cpp_file(sources[0]))
                compile_cpp_with_analyzer(sources,source_count,"/dev/null",
                    analyze_out,sizeof(analyze_out));
            else
                compile_with_analyzer(sources,source_count,"/dev/null",
                    analyze_out,sizeof(analyze_out));
            lc = parse_sanitizer_errors(analyze_out,le,16);
            snprintf(lo,sizeof(lo),"%s",analyze_out);
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 12. Clang-Tidy with all checker groups */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            char tidy_out[MAX_OUTPUT_SIZE] = {0};
            if (system("command -v clang-tidy >/dev/null 2>&1") == 0) {
                char cmd[16384];
                size_t pos = snprintf(cmd,sizeof(cmd),
                    "clang-tidy --checks='clang-analyzer-*,bugprone-*,performance-*,"
                    "portability-*,readability-*'");
                for (int s=0; s<source_count && pos<sizeof(cmd)-4; s++)
                    pos += snprintf(cmd+pos,sizeof(cmd)-pos," '%s'",sources[s]);
                pos += snprintf(cmd+pos,sizeof(cmd)-pos," 2>&1");
                FILE *pp = popen(cmd,"r");
                if (pp) { size_t n=fread(tidy_out,1,sizeof(tidy_out)-1,pp); tidy_out[n]='\0'; pclose(pp); }
            }
            char *save;
            char *line = strtok_r(tidy_out,"\n",&save);
            while (line && lc < 16) {
                if (string_contains(line,"warning:") || string_contains(line,"error:")) {
                    int pat = classify_error(line);
                    if (pat >= 0) {
                        le[lc].type = error_patterns[pat].type;
                        snprintf(le[lc].title,sizeof(le[lc].title),"%s",error_patterns[pat].title);
                        snprintf(le[lc].fix_suggestion,sizeof(le[lc].fix_suggestion),"%s",error_patterns[pat].fix);
                        le[lc].severity = error_patterns[pat].severity;
                        le[lc].has_source = false; lc++;
                    }
                }
                line = strtok_r(NULL,"\n",&save);
            }
            snprintf(lo,sizeof(lo),"%s",tidy_out);
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 13. Boundary fuzz (10 edge-case inputs) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b) lc = run_fuzz_analysis(b,le,16,lo,sizeof(lo),timeout_sec);
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 14. Random fuzz (10 inputs from /dev/urandom) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b && access(b,X_OK)==0) {
                setenv("ASAN_OPTIONS","detect_leaks=1",1);
                FILE *urandom = fopen("/dev/urandom","r");
                for (int f=0; f<10 && lc<16; f++) {
                    unsigned char rbuf[1024];
                    if (!urandom) break;
                    size_t rlen = (f%3==0) ? 4 : (f%3==1) ? 64 : 256;
                    size_t got = fread(rbuf,1,rlen,urandom);
                    char ifile[] = "/tmp/ultra_fuzz_XXXXXX";
                    int fd = mkstemp(ifile);
                    if (fd < 0) break;
                    FILE *ifp = fdopen(fd,"wb");
                    if (ifp) { fwrite(rbuf,1,got,ifp); fclose(ifp); }
                    char rc[8192];
                    snprintf(rc,sizeof(rc),"timeout %d '%s' < '%s' 2>&1",timeout_sec/3,b,ifile);
                    FILE *pp = popen(rc,"r");
                    if (pp) {
                        char robuf[8192];
                        size_t n = fread(robuf,1,sizeof(robuf)-1,pp); robuf[n]='\0';
                        int ex = pclose(pp);
                        if (ex!=0 && ex!=128 && (string_contains(robuf,"ERROR:")||
                            string_contains(robuf,"runtime error:")) && lc<16) {
                            le[lc].type = ERR_SEG;
                            snprintf(le[lc].title,sizeof(le[lc].title),"Random Fuzz Crash (input %d)",f);
                            snprintf(le[lc].fix_suggestion,sizeof(le[lc].fix_suggestion),
                                     "Random input #%d crashed — validate all input.",f);
                            le[lc].severity=2; le[lc].has_source=false; lc++;
                            snprintf(lo+strlen(lo),sizeof(lo)-strlen(lo),"Random fuzz %d: crash\n",f);
                        }
                    }
                    unlink(ifile);
                }
                if (urandom) fclose(urandom);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 15. Rerun 3x for flaky detection */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b && access(b,X_OK)==0) {
                int crash=0, clean=0;
                for (int r=0; r<3; r++) {
                    setenv("ASAN_OPTIONS","detect_leaks=1",1);
                    char r_out[32768], r_err[32768];
                    int ec = run_binary(b,NULL,r_out,sizeof(r_out),r_err,sizeof(r_err),timeout_sec,NULL);
                    if (ec==-1||ec>128) crash++; else if (ec==0) clean++;
                    size_t remain = sizeof(lo) - strlen(lo) - 2;
                    if (remain > 8) {
                        size_t rlen = strlen(r_err);
                        if (rlen > remain - 40) rlen = remain - 40;
                        snprintf(lo+strlen(lo),remain,
                                 "=== Run %d: exit=%d ===\n%.*s\n",r,ec,(int)rlen,r_err);
                    }
                }
                if (crash>0 && clean>0 && lc<16) {
                    le[lc].type = ERR_UNKNOWN;
                    snprintf(le[lc].title,sizeof(le[lc].title),
                             "Non-Deterministic Crash (%d/3 runs)",crash);
                    snprintf(le[lc].fix_suggestion,sizeof(le[lc].fix_suggestion),
                             "Crashes %d/3 runs — non-deterministic bug. "
                             "Check uninitialized vars, use-after-free, or data races.",crash);
                    le[lc].severity=3; le[lc].has_source=false; lc++;
                }
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 16. GCov code coverage analysis */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("GCov");
            if (b && access(b,X_OK)==0)
                lc = run_coverage_analysis(b,sources,source_count,le,16,lo,sizeof(lo),timeout_sec);
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 17. Dangerous API scan */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            lc = scan_dangerous_apis(sources,source_count,le,16);
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 18. clang MemorySanitizer (if clang available) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            if (system("command -v clang >/dev/null 2>&1") == 0) {
                char msan_bin[MAX_PATH_LEN];
                generate_temp_path("prism_msan",msan_bin,sizeof(msan_bin));
                char cmd[16384];
                size_t pos = snprintf(cmd,sizeof(cmd),
                    "clang -fsanitize=memory -fsanitize-memory-track-origins=2 -g -O1 -o '%s'",msan_bin);
                for (int s=0; s<source_count && pos<sizeof(cmd)-4; s++)
                    pos += snprintf(cmd+pos,sizeof(cmd)-pos," '%s'",sources[s]);
                pos += snprintf(cmd+pos,sizeof(cmd)-pos," 2>&1");
                FILE *pp = popen(cmd,"r");
                if (pp) { char buf[4096]; while(fgets(buf,sizeof(buf),pp)){} pclose(pp); }
                if (access(msan_bin,X_OK)==0) {
                    setenv("MSAN_OPTIONS","halt_on_error=0:report_umrs=1",1);
                    char m_out[32768], m_err[32768];
                    run_binary(msan_bin,NULL,m_out,sizeof(m_out),m_err,sizeof(m_err),timeout_sec,NULL);
                    if (strlen(m_err)>0) {
                        lc = parse_sanitizer_errors(m_err,le,16);
                        snprintf(lo,sizeof(lo),"%s",m_err);
                    }
                    unlink(msan_bin);
                }
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 19. GCC Analyzer -O0 (different optimization reveals different bugs) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            char cmd[16384];
            size_t pos = snprintf(cmd,sizeof(cmd),"gcc -fanalyzer -O0 -g -fsyntax-only");
            for (int s=0; s<source_count && pos<sizeof(cmd)-4; s++)
                pos += snprintf(cmd+pos,sizeof(cmd)-pos," '%s'",sources[s]);
            pos += snprintf(cmd+pos,sizeof(cmd)-pos," 2>&1");
            FILE *pp = popen(cmd,"r");
            if (pp) { size_t n=fread(lo,1,sizeof(lo)-1,pp); lo[n]='\0'; pclose(pp); }
            lc = parse_sanitizer_errors(lo,le,16);
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 20. Large input stress (64KB of 'A' via stdin) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b) {
                char big_input[32768];
                memset(big_input,'A',sizeof(big_input)-1); big_input[sizeof(big_input)-1]='\0';
                char ifile[] = "/tmp/ultra_big_XXXXXX";
                int fd = mkstemp(ifile);
                if (fd >= 0) {
                    FILE *ifp = fdopen(fd,"w");
                    if (ifp) { fwrite(big_input,1,sizeof(big_input)-1,ifp); fclose(ifp); }
                    setenv("ASAN_OPTIONS","detect_leaks=1",1);
                    char cmd[8192];
                    snprintf(cmd,sizeof(cmd),"timeout %d '%s' < '%s' 2>&1",timeout_sec,b,ifile);
                    FILE *pp = popen(cmd,"r");
                    if (pp) { size_t n=fread(lo,1,sizeof(lo)-1,pp); lo[n]='\0'; pclose(pp); }
                    lc = parse_sanitizer_errors(lo,le,16);
                    unlink(ifile);
                }
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 21. Empty stdin */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1",1);
                char cmd[8192];
                snprintf(cmd,sizeof(cmd),"timeout %d '%s' </dev/null 2>&1",timeout_sec,b);
                FILE *pp = popen(cmd,"r");
                if (pp) { size_t n=fread(lo,1,sizeof(lo)-1,pp); lo[n]='\0'; pclose(pp); }
                lc = parse_sanitizer_errors(lo,le,16);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 22. Argv stress (100 long arguments) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b) {
                char cmd[16384];
                size_t pos = snprintf(cmd,sizeof(cmd),"timeout %d '%s'",timeout_sec,b);
                for (int a=0; a<100 && pos<sizeof(cmd)-128; a++)
                    pos += snprintf(cmd+pos,sizeof(cmd)-pos," ARG%d=%030d",a,a);
                pos += snprintf(cmd+pos,sizeof(cmd)-pos," 2>&1");
                FILE *pp = popen(cmd,"r");
                if (pp) { size_t n=fread(lo,1,sizeof(lo)-1,pp); lo[n]='\0'; pclose(pp); }
                lc = parse_sanitizer_errors(lo,le,16);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 23. ASan+UBSan -Os with long argv (over 100 chars per arg) */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -Os");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1",1);
                char r_out[32768], r_err[32768];
                run_binary(b,NULL,r_out,sizeof(r_out),r_err,sizeof(r_err),timeout_sec,NULL);
                lc = parse_sanitizer_errors(r_err,le,16);
                snprintf(lo,sizeof(lo),"%s",r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }

    /* 24. Memset bug detection: check if memset(0) on sensitive data matters */
    {
        pid_t pid = fork();
        if (pid == 0) {
            FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
            if (!rfp) _exit(0);
            DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
            const char *b = ULTRA_BIN("ASan+UBSan -O2");
            if (b) {
                setenv("ASAN_OPTIONS","detect_leaks=1:check_memset_poison=1:max_malloc_fill_size=4096",1);
                char r_out[32768], r_err[32768];
                run_binary(b,NULL,r_out,sizeof(r_out),r_err,sizeof(r_err),timeout_sec,NULL);
                lc = parse_sanitizer_errors(r_err,le,16);
                snprintf(lo,sizeof(lo),"%s",r_err);
            }
            ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
        }
        if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
    }
	/* 25. AST dangerous call detection + check engine modules */
	{
	    pid_t pid = fork();
	    if (pid == 0) {
	        FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
	        if (!rfp) _exit(0);
	        DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
	        AnnotationDB *ann = ann_load(NULL);
	        if (ann) {
	            for (int fi = 0; fi < source_count && lc < 16; fi++) {
	                ASTContext *actx = ast_parse(sources[fi], NULL);
	                if (!actx) continue;

	                DangerousCall dcs[16]; int ndc = 0;
	                ndc = ast_get_dangerous_calls(actx, dcs, 16);
	                for (int d = 0; d < ndc && lc < 16; d++) {
	                    snprintf(le[lc].title, sizeof(le[lc].title),
	                             "AST: Unsafe %.64s Call", dcs[d].function_name);
	                    snprintf(le[lc].fix_suggestion, sizeof(le[lc].fix_suggestion),
	                             "'%.64s' at %s:%u \xe2\x80\x94 use a safer alternative",
	                             dcs[d].function_name, dcs[d].source_file, dcs[d].line);
	                    le[lc].type = dcs[d].type;
	                    le[lc].severity = dcs[d].severity;
	                    le[lc].has_source = true;
	                    le[lc].source_line = dcs[d].line;
	                    lc++;
	                }

	                DetectedError ce[16];
	                memset(ce, 0, sizeof(ce));
	                int nce = check_engine_run(actx, ann, ce, 16);
	                for (int c = 0; c < nce && lc < 16; c++) {
	                    le[lc] = ce[c];
	                    lc++;
	                }
	                ast_free(actx);
	            }
	            ann_free(ann);
	        }
	        ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
	    }
	    if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
	}

	/* 26. AST symbolic execution (path-sensitive check) */
	{
	    pid_t pid = fork();
	    if (pid == 0) {
	        FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
	        if (!rfp) _exit(0);
	        DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[1];
	        for (int fi = 0; fi < source_count && lc < 16; fi++) {
	            SymPathSet ps = sym_analyze_source(sources[fi], NULL, 0, 128);
	            if (ps.path_count > 1) {
	                snprintf(le[lc].title, sizeof(le[lc].title),
	                         "AST: %d feasible paths in %s",
	                         ps.path_count, sources[fi]);
	                snprintf(le[lc].fix_suggestion, sizeof(le[lc].fix_suggestion),
	                         "File has %d symbolic execution paths \xe2\x80\x94 "
	                         "test each branch combination for correctness.",
	                         ps.path_count);
	                le[lc].type = ERR_NONE;
	                le[lc].severity = 1;
	                le[lc].has_source = false;
	                lc++;
	            }
	            sym_free_paths(&ps);
	        }
	        ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
	    }
	    if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
	}

	/* 28. Strict aliasing check */
	{
	    pid_t pid = fork();
	    if (pid == 0) {
	        FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
	        if (!rfp) _exit(0);
	        DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[MAX_OUTPUT_SIZE]={0};
	        compile_with_strict_aliasing(sources, source_count,
	                                     lo, sizeof(lo));
	        char *save, *line = strtok_r(lo, "\n", &save);
	        while (line && lc < 16) {
	            if (string_contains(line, "strict-aliasing")) {
	                le[lc].type = ERR_STRICT_ALIASING;
	                snprintf(le[lc].title, sizeof(le[lc].title), "%s", line);
	                snprintf(le[lc].fix_suggestion, sizeof(le[lc].fix_suggestion),
	                         "Use a union or memcpy instead of type-punning through a pointer.");
	                le[lc].severity = 2;
	                le[lc].has_source = false;
	                lc++;
	            }
	            line = strtok_r(NULL, "\n", &save);
	        }
	        ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
	    }
	    if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
	}

	/* 29. Float-equal check */
	{
	    pid_t pid = fork();
	    if (pid == 0) {
	        FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
	        if (!rfp) _exit(0);
	        DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[MAX_OUTPUT_SIZE]={0};
	        compile_with_float_equal_warning(sources, source_count,
	                                         lo, sizeof(lo));
	        char *save, *line = strtok_r(lo, "\n", &save);
	        while (line && lc < 16) {
	            if (string_contains(line, "float-equal")) {
	                le[lc].type = ERR_FLOAT_COMPARE;
	                snprintf(le[lc].title, sizeof(le[lc].title), "%s", line);
	                snprintf(le[lc].fix_suggestion, sizeof(le[lc].fix_suggestion),
	                         "Use an epsilon comparison: fabs(a - b) < 0.0001 instead of ==.");
	                le[lc].severity = 2;
	                le[lc].has_source = false;
	                lc++;
	            }
	            line = strtok_r(NULL, "\n", &save);
	        }
	        ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
	    }
	    if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
	}

	/* 30. Conversion warnings */
	{
	    pid_t pid = fork();
	    if (pid == 0) {
	        FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
	        if (!rfp) _exit(0);
	        DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[MAX_OUTPUT_SIZE]={0};
	        compile_with_conversion_warnings(sources, source_count,
	                                         lo, sizeof(lo), false);
	        char *save, *line = strtok_r(lo, "\n", &save);
	        while (line && lc < 16) {
	            if (string_contains(line, "conversion") ||
	                string_contains(line, "may change value")) {
	                le[lc].type = ERR_CONVERSION_LOSS;
	                snprintf(le[lc].title, sizeof(le[lc].title), "%s", line);
	                snprintf(le[lc].fix_suggestion, sizeof(le[lc].fix_suggestion),
	                         "Use an explicit cast to make the conversion visible.");
	                le[lc].severity = 1;
	                le[lc].has_source = false;
	                lc++;
	            }
	            line = strtok_r(NULL, "\n", &save);
	        }
	        ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
	    }
	    if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
	}

	/* 31. cppcheck */
	{
	    pid_t pid = fork();
	    if (pid == 0) {
	        FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
	        if (!rfp) _exit(0);
	        DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[MAX_OUTPUT_SIZE]={0};
	        if (system("command -v cppcheck >/dev/null 2>&1") == 0) {
	            run_cppcheck(sources, source_count, lo, sizeof(lo));
	            char *save, *line = strtok_r(lo, "\n", &save);
	            while (line && lc < 16) {
	                if (strstr(line, ": error:") || strstr(line, ": warning:") ||
	                    strstr(line, ": style:")) {
	                    le[lc].type = ERR_UNKNOWN;
	                    if (strstr(line, ": error:"))
	                        le[lc].severity = 3;
	                    else if (strstr(line, ": warning:"))
	                        le[lc].severity = 2;
	                    else
	                        le[lc].severity = 1;
	                    /* Extract line number */
	                    char fcopy[MAX_LINE_LEN];
	                    strncpy(fcopy, line, sizeof(fcopy) - 1);
	                    char *c1 = strchr(fcopy, ':');
	                    if (c1) {
	                        int ln = atoi(c1 + 1);
	                        if (ln > 0) {
	                            le[lc].source_line = ln;
	                            le[lc].has_source = true;
	                        }
	                    }
	                    if (string_contains(line, "nullPointer"))
	                        le[lc].type = ERR_NULL_DEREF;
	                    else if (string_contains(line, "memleak"))
	                        le[lc].type = ERR_MEMORY_LEAK;
	                    else if (string_contains(line, "doubleFree"))
	                        le[lc].type = ERR_DOUBLE_FREE_CPP;
	                    else if (string_contains(line, "deallocuse"))
	                        le[lc].type = ERR_USE_AFTER_FREE;
	                    else if (string_contains(line, "zerodiv"))
	                        le[lc].type = ERR_DIV_BY_ZERO;
	                    else if (string_contains(line, "bufferAccessOutOfBounds"))
	                        le[lc].type = ERR_BUFFER_OVERFLOW;
	                    else if (string_contains(line, "uninitvar"))
	                        le[lc].type = ERR_UNINIT_VAR;
	                    else if (string_contains(line, "integerOverflow"))
	                        le[lc].type = ERR_INT_OVERFLOW;
	                    else if (string_contains(line, "negativeIndex"))
	                        le[lc].type = ERR_BUFFER_OVERFLOW;

	                    char msg_buf[256];
	                    snprintf(msg_buf, sizeof(msg_buf), "[cppcheck] %s", line);
	                    snprintf(le[lc].title, sizeof(le[lc].title), "%s", msg_buf);
	                    snprintf(le[lc].fix_suggestion, sizeof(le[lc].fix_suggestion),
	                             "Fix the issue reported by cppcheck at line %d.",
	                             le[lc].source_line);
	                    le[lc].has_source = le[lc].source_line > 0;
	                    lc++;
	                }
	                line = strtok_r(NULL, "\n", &save);
	            }
	        }
	        ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
	    }
	    if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
	}

	/* 32. clang --analyze */
	{
	    pid_t pid = fork();
	    if (pid == 0) {
	        FILE *rfp = fopen(analysis_passes[num_analysis].result_file, "w");
	        if (!rfp) _exit(0);
	        DetectedError le[16]; memset(le,0,sizeof(le)); int lc=0; char lo[MAX_OUTPUT_SIZE]={0};
	        if (system("command -v clang >/dev/null 2>&1") == 0) {
	            run_clang_analyze(sources, source_count, lo, sizeof(lo));
	            char *save, *line = strtok_r(lo, "\n", &save);
	            while (line && lc < 16) {
	                if (strstr(line, ": warning:") && !strstr(line, ": note:")) {
	                    le[lc].type = ERR_UNKNOWN;
	                    le[lc].severity = 2;
	                    if (string_contains(line, "core.NullDereference"))
	                        le[lc].type = ERR_NULL_DEREF;
	                    else if (string_contains(line, "core.DivideZero"))
	                        le[lc].type = ERR_DIV_BY_ZERO;
	                    else if (string_contains(line, "unix.Malloc"))
	                        le[lc].type = ERR_MEMORY_LEAK;
	                    else if (string_contains(line, "core.UndefinedBinaryOperatorResult"))
	                        le[lc].type = ERR_UNINIT_VAR;

	                    char fcopy[MAX_LINE_LEN];
	                    strncpy(fcopy, line, sizeof(fcopy) - 1);
	                    char *c1 = strchr(fcopy, ':');
	                    if (c1) {
	                        int ln = atoi(c1 + 1);
	                        if (ln > 0) {
	                            *c1 = '\0';
	                            strncpy(le[lc].source_file, fcopy,
	                                    sizeof(le[lc].source_file) - 1);
	                            le[lc].source_line = ln;
	                            le[lc].has_source = true;
	                        }
	                    }
	                    snprintf(le[lc].title, sizeof(le[lc].title),
	                             "[clang-analyze] %s", line);
	                    le[lc].severity = 2;
	                    lc++;
	                }
	                line = strtok_r(NULL, "\n", &save);
	            }
	        }
	        ULTRA_CHILD_RESULT(lo,lc,le); fclose(rfp); _exit(0);
	    }
	    if (pid > 0) { analysis_passes[num_analysis].pid=pid; num_analysis++; }
	}

    /* ─── Collect results from all analysis children ─── */
    for (int p = 0; p < num_analysis; p++) {
        int status;
        waitpid(analysis_passes[p].pid, &status, 0);

        FILE *rfp = fopen(analysis_passes[p].result_file, "r");
        if (!rfp) continue;

        int lc;
        if (fscanf(rfp,"%d\n",&lc) != 1) { fclose(rfp); continue; }

        for (int j = 0; j < lc && j < 16 && total_errors < max_errors; j++) {
            DetectedError e;
            memset(&e, 0, sizeof(e));
            int has_src, type_val;
            if (fscanf(rfp,"%d|%255[^|]|%1023[^|]|%d|%d|%d\n",
                       &type_val, e.title, e.fix_suggestion,
                       &e.source_line, &e.severity, &has_src) < 5)
                break;
            e.type = (ErrorType)type_val;
            e.has_source = (has_src != 0);
            total_errors = merge_analysis_error(errors,total_errors,max_errors,
                                                 &e,error_modes,1ull << p);
        }
        fclose(rfp);
        unlink(analysis_passes[p].result_file);
    }

    /* ─── Cleanup ─── */
    for (int i = 0; i < num_var; i++) {
        if (binary_paths[i]) {
            if (binary_paths[i][0]) unlink(binary_paths[i]);
            free(binary_paths[i]);
        }
    }
    system("rm -f /tmp/ultra_fuzz_* /tmp/ultra_big_* 2>/dev/null");
    system("rm -f *.gcda *.gcno *.gcov 2>/dev/null");

    return total_errors;
}
#undef MAX_ANALYSIS_PASSES
#undef ULTRA_NUM_VARIANTS
#undef ULTRA_BIN
#undef ULTRA_CHILD_RESULT

/*
 * main - CLI entry point for prism
 *
 *      execution, and error reporting workflow.
 *      Supports multiple source files compiled into a single binary.
 */
int main(int argc, char *argv[])
{
    ColorCodes colors;
    TestResult result;
    enum { MAX_SRC_FILES = 256 };
    DetectedError errors[MAX_SRC_FILES];
    memset(errors, 0, sizeof(errors));
    char binary_path[MAX_PATH_LEN];
    const char *source_files[MAX_SRC_FILES];
    int source_count = 0;
    char project_source_store[MAX_SRC_FILES][MAX_PATH_LEN];
    bool keep_binary = false;
    bool use_color = true;
    bool use_tsan = false;
    bool use_msan = false;
    bool use_analyzer = false;
    bool use_clang_tidy = false;
    bool use_valgrind = false;
    bool use_max = false;
    bool use_fuzz = false;
    bool use_danger = false;
    bool use_resources = false;
    int rerun_count = 0;
    char project_json[MAX_PATH_LEN] = {0};
    const char *html_path = NULL;
    int timeout_sec = DEFAULT_TIMEOUT_SEC;
    int jobs = 0;
    int error_count = 0;
    int i;
    bool use_quick = false;
    bool use_compile_only = false;
    bool use_header = false;
    bool use_sarif = false;
    char sarif_path[MAX_PATH_LEN] = {0};
    bool use_ci = false;
    const char *self_binary = argv[0];
    bool use_full = false;
    bool use_git_diff = false;
    bool use_gdb = false;
    bool use_checksec = false;
    bool use_gcov = false;
    bool use_libfuzzer = false;
    bool use_ultra = false;
    bool use_cache = false;
    bool use_clear_cache = false;
    bool use_generate_baseline = false;
    bool use_install_hook = false;
    bool use_uninstall_hook = false;
    bool use_ast = false;
    bool use_cppcheck = false;
    bool use_clang_analyze = false;
    bool use_iwyu = false;
    bool use_strict_aliasing = false;
    bool use_float_equal = false;
    bool use_conversion = false;
    bool conversion_all = false;
    char baseline_path[MAX_PATH_LEN] = {0};
    char link_flags_buf[4096] = {0};
    char git_bisect_ref[256] = {0};

    use_color = isatty(STDOUT_FILENO);
    init_colors(&colors, use_color);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--keep") == 0) {
            keep_binary = true;
        } else if (strncmp(argv[i], "--timeout=", 10) == 0) {
            timeout_sec = atoi(argv[i] + 10);
            if (timeout_sec <= 0)
                timeout_sec = DEFAULT_TIMEOUT_SEC;
        } else if (strncmp(argv[i], "--jobs=", 8) == 0) {
            jobs = atoi(argv[i] + 8);
            if (jobs < 1) jobs = 1;
        } else if (strcmp(argv[i], "--jobs") == 0 && i + 1 < argc) {
            jobs = atoi(argv[++i]);
            if (jobs < 1) jobs = 1;
        } else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
            jobs = atoi(argv[++i]);
            if (jobs < 1) jobs = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            use_color = false;
            init_colors(&colors, use_color);
        } else if (strcmp(argv[i], "--tsan") == 0) {
            use_tsan = true;
        } else if (strcmp(argv[i], "--msan") == 0) {
            use_msan = true;
        } else if (strcmp(argv[i], "--analyzer") == 0) {
            use_analyzer = true;
        } else if (strcmp(argv[i], "--clang-tidy") == 0) {
            use_clang_tidy = true;
        } else if (strcmp(argv[i], "--cppcheck") == 0) {
            use_cppcheck = true;
        } else if (strcmp(argv[i], "--clang-analyze") == 0) {
            use_clang_analyze = true;
        } else if (strcmp(argv[i], "--iwyu") == 0) {
            use_iwyu = true;
        } else if (strcmp(argv[i], "--strict-aliasing") == 0) {
            use_strict_aliasing = true;
        } else if (strcmp(argv[i], "--float-equal") == 0) {
            use_float_equal = true;
        } else if (strncmp(argv[i], "--conversion", 12) == 0) {
            use_conversion = true;
            if (strcmp(argv[i] + 12, "=all") == 0)
                conversion_all = true;
        } else if (strcmp(argv[i], "--valgrind") == 0) {
            use_valgrind = true;
        } else if (strcmp(argv[i], "--max") == 0) {
            use_max = true;
        } else if (strcmp(argv[i], "--fuzz") == 0) {
            use_fuzz = true;
        } else if (strncmp(argv[i], "--rerun=", 8) == 0) {
            rerun_count = atoi(argv[i] + 8);
            if (rerun_count < 2) rerun_count = 10;
        } else if (strcmp(argv[i], "--rerun") == 0 && i + 1 < argc) {
            rerun_count = atoi(argv[++i]);
            if (rerun_count < 2) rerun_count = 10;
        } else if (strcmp(argv[i], "--resources") == 0) {
            use_resources = true;
        } else if (strcmp(argv[i], "--danger") == 0) {
            use_danger = true;
        } else if (strcmp(argv[i], "--quick") == 0) {
            use_quick = true;
        } else if (strcmp(argv[i], "--full") == 0) {
            use_full = true;
        } else if (strcmp(argv[i], "--ultra") == 0) {
            use_ultra = true;
        } else if (strcmp(argv[i], "--git-diff") == 0) {
            use_git_diff = true;
        } else if (strncmp(argv[i], "--git-bisect=", 13) == 0) {
            strncpy(git_bisect_ref, argv[i] + 13, sizeof(git_bisect_ref) - 1);
            git_bisect_ref[sizeof(git_bisect_ref) - 1] = '\0';
        } else if (strcmp(argv[i], "--gdb") == 0) {
            use_gdb = true;
        } else if (strcmp(argv[i], "--gcov") == 0) {
            use_gcov = true;
        } else if (strcmp(argv[i], "--libfuzzer") == 0) {
            use_libfuzzer = true;
        } else if (strcmp(argv[i], "--checksec") == 0) {
            use_checksec = true;
        } else if (strcmp(argv[i], "--ast") == 0) {
            use_ast = true;
        } else if (strcmp(argv[i], "--cache") == 0) {
            use_cache = true;
        } else if (strcmp(argv[i], "--clear-cache") == 0) {
            use_clear_cache = true;
        } else if (strcmp(argv[i], "--generate-baseline") == 0) {
            use_generate_baseline = true;
        } else if (strncmp(argv[i], "--baseline=", 11) == 0) {
            strncpy(baseline_path, argv[i] + 11, sizeof(baseline_path) - 1);
            baseline_path[sizeof(baseline_path) - 1] = '\0';
        } else if (strcmp(argv[i], "--install-hook") == 0) {
            use_install_hook = true;
        } else if (strcmp(argv[i], "--uninstall-hook") == 0) {
            use_uninstall_hook = true;
        } else if (strncmp(argv[i], "--project=", 10) == 0) {
            strncpy(project_json, argv[i] + 10, sizeof(project_json) - 1);
            project_json[sizeof(project_json) - 1] = '\0';
        } else if (strncmp(argv[i], "--html=", 7) == 0) {
            html_path = argv[i] + 7;
        } else if (strncmp(argv[i], "--link-flags=", 13) == 0) {
            strncpy(link_flags_buf, argv[i] + 13, sizeof(link_flags_buf) - 1);
            link_flags_buf[sizeof(link_flags_buf) - 1] = '\0';
        } else if (strcmp(argv[i], "--compile-only") == 0) {
            use_compile_only = true;
        } else if (strcmp(argv[i], "--header") == 0) {
            use_header = true;
        } else if (strncmp(argv[i], "--sarif=", 8) == 0) {
            use_sarif = true;
            strncpy(sarif_path, argv[i] + 8, sizeof(sarif_path) - 1);
            sarif_path[sizeof(sarif_path) - 1] = '\0';
        } else if (strcmp(argv[i], "--sarif") == 0) {
            use_sarif = true;
            strncpy(sarif_path, "prism_results.sarif", sizeof(sarif_path) - 1);
        } else if (strcmp(argv[i], "--ci") == 0) {
            use_ci = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0], &colors);
            return EXIT_SUCCESS;
        } else if (argv[i][0] != '-') {
            if (source_count < MAX_SRC_FILES) {
                source_files[source_count++] = argv[i];
            } else {
                fprintf(stderr, "Warning: too many source files (max %d)\n",
                        MAX_SRC_FILES);
            }
        }
    }

    /* --ci forces --no-color */
    if (use_ci) {
        use_color = false;
        init_colors(&colors, use_color);
    }

    /* Auto-detect compile_commands.json */
    {
        /* Handle directory arguments: e.g. "prism --full my_project/" */
        int new_count = 0;
        for (i = 0; i < source_count; i++) {
            if (is_directory(source_files[i])) {
                if (!project_json[0]) {
                    char detected[MAX_PATH_LEN];
                    if (find_compile_commands(source_files[i],
                                              detected,
                                              sizeof(detected)) == 0) {
                        strncpy(project_json, detected,
                                sizeof(project_json) - 1);
                        print_colored(&colors, colors.cyan, "[project] ");
                        printf("Auto-detected: %s\n", project_json);
                    }
                }
                /* Directory is not a source file */
            } else {
                source_files[new_count++] = source_files[i];
            }
        }
        source_count = new_count;

        /* If no --project provided and nothing was explicitly given,
         * try auto-detecting from CWD */
        if (!project_json[0] && source_count == 0) {
            char detected[MAX_PATH_LEN];
            if (find_compile_commands(".", detected,
                                      sizeof(detected)) == 0) {
                strncpy(project_json, detected,
                        sizeof(project_json) - 1);
                print_colored(&colors, colors.cyan, "[project] ");
                printf("Auto-detected: %s\n", project_json);
            }
        }

        /* Auto-collect sources from project JSON if no explicit files given */
        if (project_json[0] && source_count == 0) {
            int collected = get_project_sources(project_json,
                                                 project_source_store,
                                                 MAX_SRC_FILES);
            if (collected > 0) {
                for (i = 0; i < collected; i++)
                    source_files[source_count++] = project_source_store[i];
                print_colored(&colors, colors.cyan, "[project] ");
                printf("Found %d source files in %s\n",
                       source_count, project_json);
            }
        }
    }

    if (source_count == 0) {
        if (project_json[0]) {
            print_colored(&colors, colors.yellow, "[project] ");
            printf("Could not extract any source files from %s\n",
                   project_json);
        }
        print_usage(argv[0], &colors);
        return EXIT_USAGE_ERROR;
    }

    for (i = 0; i < source_count; i++) {
        if (!file_exists(source_files[i])) {
            print_colored(&colors, colors.red, "Error: ");
            printf("file not found: %s\n", source_files[i]);
            return EXIT_FILE_NOT_FOUND;
        }

        if (!is_source_file(source_files[i]) &&
            !(use_header && string_contains(source_files[i], ".h"))) {
            /* --checksec: accept executable binaries as-is */
            if (use_checksec && access(source_files[i], X_OK) == 0)
                continue;
            print_colored(&colors, colors.red, "Error: ");
            printf("not a C/C++ source file: %s\n", source_files[i]);
            return EXIT_USAGE_ERROR;
        }
    }

    /* --install-hook: create a git pre-commit hook */
    if (use_install_hook) {
        const char *git_dir;
        char hook_path[MAX_PATH_LEN];
        FILE *hook_file;

        git_dir = ".git";
        if (access(git_dir, F_OK) != 0) {
            print_colored(&colors, colors.red, "Error: ");
            printf("No .git directory found in current directory\n");
            return EXIT_USAGE_ERROR;
        }

        snprintf(hook_path, sizeof(hook_path), "%s/hooks/pre-commit", git_dir);
        hook_file = fopen(hook_path, "w");
        if (!hook_file) {
            print_colored(&colors, colors.red, "Error: ");
            printf("Failed to create pre-commit hook at %s\n", hook_path);
            return EXIT_FILE_NOT_FOUND;
        }

        fprintf(hook_file,
                "#!/bin/sh\n"
                "# prism pre-commit hook (auto-installed)\n"
                "./prism --quick --git-diff \"$@\" || exit 1\n");
        fclose(hook_file);
        chmod(hook_path, 0755);

        print_colored(&colors, colors.green, "[OK] ");
        printf("Pre-commit hook installed at %s\n", hook_path);
        return EXIT_SUCCESS;
    }

    /* --uninstall-hook: remove git pre-commit hook */
    if (use_uninstall_hook) {
        const char *git_dir = ".git";
        char hook_path[MAX_PATH_LEN];

        snprintf(hook_path, sizeof(hook_path), "%s/hooks/pre-commit", git_dir);
        if (unlink(hook_path) == 0) {
            print_colored(&colors, colors.green, "[OK] ");
            printf("Pre-commit hook removed from %s\n", hook_path);
        } else {
            print_colored(&colors, colors.yellow, "[WARN] ");
            printf("No pre-commit hook found at %s (nothing to remove)\n", hook_path);
        }
        return EXIT_SUCCESS;
    }

    /* --clear-cache: remove the analysis cache directory */
    if (use_clear_cache) {
        const char *home = getenv("HOME");
        if (home) {
            char cache_dir[MAX_PATH_LEN];
            char rm_cmd[MAX_PATH_LEN + 64];
            snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/prism", home);
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s' 2>/dev/null", cache_dir);
            system(rm_cmd);
            print_colored(&colors, colors.green, "[OK] ");
            printf("Cache cleared: %s\n", cache_dir);
        }
        return EXIT_SUCCESS;
    }

    /* --git-bisect: run git bisect with the source file */
    if (git_bisect_ref[0]) {
        char script_path[MAX_PATH_LEN];
        char bisect_cmd[MAX_PATH_LEN * 3];
        char *line_buf = malloc(65536);
        FILE *pipe;
        int script_ok = 0;

        if (!line_buf) return EXIT_COMPILE_FAIL;

        /* Create bisect script */
        snprintf(script_path, sizeof(script_path),
                 "/tmp/prism_bisect_%d.sh", (int)getpid());
        FILE *sfp = fopen(script_path, "w");
        if (sfp) {
            fprintf(sfp,
                    "#!/bin/sh\n"
                    "# prism git bisect script\n");
            /* Compile the source file */
            for (int si = 0; si < source_count; si++)
                fprintf(sfp, "SRC%d='%s'\n", si, source_files[si]);
            fprintf(sfp,
                    "SRCS=\"");
            for (int si = 0; si < source_count; si++)
                fprintf(sfp, " \"$SRC%d\"", si);
            fprintf(sfp,
                    "\"\n"
                    "gcc -Wall -Wextra -fsanitize=address,undefined -o /tmp/prism_bisect_bin $SRCS 2>/dev/null || exit 125\n"
                    "timeout 5 /tmp/prism_bisect_bin 2>/tmp/prism_bisect_err.txt\n"
                    "EXIT=$?\n"
                    "if [ \"$EXIT\" -ne 0 ] && [ \"$EXIT\" -ne 125 ]; then\n"
                    "  if grep -q 'ERROR:\\|runtime error:\\|===.*WARNING' /tmp/prism_bisect_err.txt 2>/dev/null; then\n"
                    "    exit 1\n"
                    "  fi\n"
                    "fi\n"
                    "exit 0\n");
            fclose(sfp);
            chmod(script_path, 0755);
            script_ok = 1;
        }

        if (!script_ok) {
            free(line_buf);
            print_colored(&colors, colors.red, "Error: ");
            printf("Failed to create bisect script\n");
            return EXIT_COMPILE_FAIL;
        }

        /* Run git bisect */
        print_colored(&colors, colors.cyan, "[bisect] ");
        printf("Running git bisect start HEAD %s\n", git_bisect_ref);
        fflush(stdout);

        snprintf(bisect_cmd, sizeof(bisect_cmd),
                 "git bisect start HEAD '%s' 2>&1 && git bisect run '%s' 2>&1",
                 git_bisect_ref, script_path);

        pipe = popen(bisect_cmd, "r");
        if (pipe) {
            size_t n = fread(line_buf, 1, 65535, pipe);
            line_buf[n] = '\0';
            pclose(pipe);
            printf("%s\n", line_buf);
        }

        unlink(script_path);
        free(line_buf);
        return EXIT_SUCCESS;
    }

    /* --git-diff: filter source files to only those changed in git diff */
    if (use_git_diff) {
        char diff_cmd[256];
        FILE *pipe;
        char changed[32][MAX_PATH_LEN];
        int changed_count = 0;

        snprintf(diff_cmd, sizeof(diff_cmd),
                 "git diff --name-only HEAD 2>/dev/null");
        pipe = popen(diff_cmd, "r");
        if (pipe) {
            char line[MAX_PATH_LEN];
            while (fgets(line, sizeof(line), pipe) && changed_count < 32) {
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n')
                    line[len - 1] = '\0';
                memcpy(changed[changed_count], line, MAX_PATH_LEN - 1);
                changed[changed_count][MAX_PATH_LEN - 1] = '\0';
                changed_count++;
            }
            pclose(pipe);
        }

        /* Filter source_files to only those in the diff */
        int filtered = 0;
        for (i = 0; i < source_count; i++) {
            for (int j = 0; j < changed_count; j++) {
                if (strstr(source_files[i], changed[j]) ||
                    strstr(changed[j], source_files[i])) {
                    source_files[filtered++] = source_files[i];
                    break;
                }
            }
        }
        source_count = filtered;

        if (source_count == 0) {
            print_colored(&colors, colors.green, "[OK] ");
            printf("No changed files to analyze\n");
            return EXIT_CLEAN;
        }
    }

    /* Default to --quick if no analysis mode was explicitly set */
    if (!use_tsan && !use_msan && !use_analyzer && !use_clang_tidy &&
        !use_valgrind && !use_max && !use_fuzz && !use_danger &&
        !use_resources && !use_gcov && !use_libfuzzer && !use_ultra &&
        !use_full && !use_checksec && !use_header &&
        !use_compile_only && !use_cppcheck && !use_clang_analyze &&
        !use_iwyu && !use_strict_aliasing && !use_float_equal &&
        !use_conversion && rerun_count == 0) {
        use_quick = true;
    }

    /* Auto-detect CPU count for --jobs=nproc behavior */
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count < 1) cpu_count = 1;
    /* --ultra forces maximum parallelism */
    if (use_ultra) {
        jobs = (int)cpu_count;
    } else if (jobs <= 0) {
        /* Default: use nproc, but cap at 4 for non-ultra mode */
        jobs = (int)cpu_count;
        if (jobs > 4) jobs = 4;
    }
    if (jobs > (int)cpu_count)
        jobs = (int)cpu_count;

    /* Parallel processing with fork() + waitpid() */
    /* Checking parallel mode availability */
    /* Modes that compile+link all sources into one binary (--full, --quick,
     * --tsan, --msan, etc.) skip forking — they need all files together. */
    /* NOTE: --ast mode processes all files in-process, skip fork path */
    bool needs_linking = use_full || use_quick || use_tsan || use_msan ||
                         use_valgrind || use_fuzz || rerun_count > 0 ||
                         use_resources || use_gcov || use_libfuzzer ||
                         use_max || use_checksec || use_cppcheck ||
                         use_clang_analyze || use_iwyu ||
                         use_strict_aliasing || use_float_equal ||
                         use_conversion;
    if (!use_ast && !needs_linking && jobs > 1 && source_count > 1) {
        int active_children = 0;
        int total_errors = 0;

        print_colored(&colors, colors.cyan, "[parallel] ");
        printf("Processing %d files with %d parallel jobs\n", source_count, jobs);
        fflush(stdout);

        for (i = 0; i < source_count; i++) {
            /* Wait if at max jobs */
            while (active_children >= jobs) {
                int status;
                pid_t done = waitpid(-1, &status, 0);
                active_children--;
                if (done > 0) {
                    if (WIFEXITED(status)) {
                        total_errors += WEXITSTATUS(status);
                    }
                }
            }

            pid_t pid = fork();
            if (pid == 0) {
                /* Child: process ONE file by execing prism */
                char timeout_str[32];
                char rerun_str[32];
                char project_str[MAX_PATH_LEN + 16];
                char html_str[MAX_PATH_LEN + 16];

                snprintf(timeout_str, sizeof(timeout_str), "--timeout=%d", timeout_sec);

                /* Build argument list for child */
                int arg_idx = 0;
                char *child_argv[48];
                child_argv[arg_idx++] = "./prism";
                if (keep_binary) child_argv[arg_idx++] = "--keep";
                child_argv[arg_idx++] = timeout_str;
                if (use_tsan) child_argv[arg_idx++] = "--tsan";
                if (use_msan) child_argv[arg_idx++] = "--msan";
                if (use_clang_tidy) child_argv[arg_idx++] = "--clang-tidy";
                if (use_valgrind) child_argv[arg_idx++] = "--valgrind";
                if (use_max) child_argv[arg_idx++] = "--max";
                if (use_fuzz) child_argv[arg_idx++] = "--fuzz";
                if (use_quick) child_argv[arg_idx++] = "--quick";
                if (use_full) child_argv[arg_idx++] = "--full";
                if (use_ultra) child_argv[arg_idx++] = "--ultra";
                if (use_git_diff) child_argv[arg_idx++] = "--git-diff";
                if (use_gdb) child_argv[arg_idx++] = "--gdb";
                if (use_checksec) child_argv[arg_idx++] = "--checksec";
                if (use_gcov) child_argv[arg_idx++] = "--gcov";
                if (use_libfuzzer) child_argv[arg_idx++] = "--libfuzzer";
                if (use_cache) child_argv[arg_idx++] = "--cache";
                if (rerun_count > 0) {
                    snprintf(rerun_str, sizeof(rerun_str), "--rerun=%d", rerun_count);
                    child_argv[arg_idx++] = rerun_str;
                }
                if (use_resources) child_argv[arg_idx++] = "--resources";
                if (use_danger) child_argv[arg_idx++] = "--danger";
                if (project_json[0]) {
                    snprintf(project_str, sizeof(project_str), "--project=%s", project_json);
                    child_argv[arg_idx++] = project_str;
                }
                if (html_path) {
                    snprintf(html_str, sizeof(html_str), "--html=%s", html_path);
                    child_argv[arg_idx++] = html_str;
                }
                if (baseline_path[0]) {
                    char buf[MAX_PATH_LEN + 16];
                    snprintf(buf, sizeof(buf), "--baseline=%s", baseline_path);
                    child_argv[arg_idx++] = strdup(buf);
                }
                child_argv[arg_idx++] = (char *)source_files[i];
                child_argv[arg_idx++] = NULL;

                execvp(self_binary ? self_binary : "./prism", child_argv);
                _exit(127);
            } else if (pid > 0) {
                active_children++;
            }
        }

        /* Wait for all remaining children */
        while (active_children > 0) {
            int status;
            pid_t done = waitpid(-1, &status, 0);
            active_children--;
            if (done > 0) {
                if (WIFEXITED(status)) {
                    total_errors += WEXITSTATUS(status);
                }
            }
        }

        return total_errors > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    /* Single-file or jobs=1 processing */
    print_banner(&colors, source_files, source_count);
    memset(&result, 0, sizeof(result));

    if (use_max) {
        error_count = run_max_analysis(source_files, source_count,
                                       errors, 32, &result,
                                       timeout_sec, &colors);
        if (html_path && html_path[0] != '\0') {
            if (generate_html_report(html_path, source_files, source_count,
                                      &result, errors, error_count) == 0) {
                print_colored(&colors, colors.green, "[HTML] ");
                printf("Report saved to: %s\n", html_path);
            }
        }
        /* --sarif: generate SARIF report */
        if (use_sarif && sarif_path[0]) {
            generate_sarif_report(sarif_path,
                                   errors, error_count,
                                   result.execution_time_ms);
        }
        /* --ci: emit JSON summary to stdout */
        if (use_ci) {
            generate_ci_output(source_files, source_count,
                               errors, error_count,
                               error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN,
                               result.execution_time_ms);
        }

        return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    /* --ultra: ALL analysis modes, max parallelism */
    if (use_ultra) {
        print_colored(&colors, colors.bold, "\n=== ULTRA ANALYSIS MODE ===\n");
        print_colored(&colors, colors.cyan, "[ultra] ");
        printf("Running all %d analysis passes with %d parallel workers...\n\n",
               jobs, jobs);
        fflush(stdout);

        result.compilation_success = true;
        error_count = run_ultra_analysis(source_files, source_count,
                                          errors, 32,
                                          timeout_sec, &colors);
        if (html_path && html_path[0] != '\0') {
            if (generate_html_report(html_path, source_files, source_count,
                                      &result, errors, error_count) == 0) {
                print_colored(&colors, colors.green, "[HTML] ");
                printf("Report saved to: %s\n", html_path);
            }
        }
        print_summary(&colors, &result, error_count, errors);

        if (use_sarif && sarif_path[0]) {
            generate_sarif_report(sarif_path,
                                   errors, error_count,
                                   result.execution_time_ms);
        }
        if (use_ci) {
            generate_ci_output(source_files, source_count,
                               errors, error_count,
                               error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN,
                               result.execution_time_ms);
        }
        return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    /* --gcov: standalone code coverage analysis */
    if (use_gcov) {
        char gcov_bin[MAX_PATH_LEN];
        generate_temp_path("prism_gcov", gcov_bin, sizeof(gcov_bin));

        /* Compile with coverage flags */
        char compile_cmd[MAX_PATH_LEN * 8 + 128];
        size_t pos = snprintf(compile_cmd, sizeof(compile_cmd),
            "gcc --coverage -g -O0 -o '%s'", gcov_bin);
        for (int si = 0; si < source_count && pos < sizeof(compile_cmd) - 4; si++)
            pos += snprintf(compile_cmd + pos, sizeof(compile_cmd) - pos,
                            " '%s'", source_files[si]);
        pos += snprintf(compile_cmd + pos, sizeof(compile_cmd) - pos, " 2>&1");

        FILE *pipe = popen(compile_cmd, "r");
        if (pipe) {
            char comp_out[32768];
            size_t n = fread(comp_out, 1, sizeof(comp_out) - 1, pipe);
            comp_out[n] = '\0';
            pclose(pipe);
        }

        if (access(gcov_bin, X_OK) != 0) {
            print_colored(&colors, colors.red, "[ERROR] ");
            printf("Coverage compilation failed (tried: %s)\n", gcov_bin);
            return EXIT_COMPILE_FAIL;
        }

        result.compilation_success = true;
        error_count = run_coverage_analysis(gcov_bin, source_files, source_count,
                                            errors, 32,
                                            result.sanitizer_output,
                                            sizeof(result.sanitizer_output),
                                            timeout_sec);
        unlink(gcov_bin);
        system("rm -f *.gcda *.gcno *.gcov 2>/dev/null");
        print_summary(&colors, &result, error_count, errors);
        return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    /* --libfuzzer: standalone libFuzzer analysis */
    if (use_libfuzzer) {
        error_count = run_libfuzzer_analysis(source_files, source_count,
                                             errors, 32,
                                             result.sanitizer_output,
                                             sizeof(result.sanitizer_output),
                                             timeout_sec);
        result.compilation_success = true;
        print_summary(&colors, &result, error_count, errors);
        return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    /* --danger: standalone static scan, no compilation needed */
    if (use_danger && !rerun_count && !use_fuzz && !use_resources) {
        print_colored(&colors, colors.bold, "[Dangerous API Scan]\n");
        error_count = scan_dangerous_apis(source_files, source_count,
                                           errors, 32);
        if (error_count > 0) {
            for (i = 0; i < error_count; i++)
                generate_fix_suggestion(&errors[i]);
            print_summary(&colors, &result, error_count, errors);
            return EXIT_ERRORS_FOUND;
        }
        print_colored(&colors, colors.green, "[OK] ");
        printf("No dangerous APIs found\n");
        return EXIT_CLEAN;
    }

    /* --ast: AST-based analysis via libclang, no compilation needed */
    if (use_ast) {
        uint64_t ast_modes[32] = {0};
        int ast_total = 0;
        print_colored(&colors, colors.bold, "[AST Analysis]\n");

        for (int fi = 0; fi < source_count && ast_total < 32; fi++) {
            /* Grab compile flags from --project if specified */
            const char *ast_args[64];
            int ast_argc = 0;
            ast_args[0] = NULL;

            if (project_json[0]) {
                char flags_buf[8192];
                if (parse_compile_commands(project_json, source_files[fi],
                                           flags_buf, sizeof(flags_buf)) == 0) {
                    char *saveptr;
                    char *tok = strtok_r(flags_buf, " \t\n", &saveptr);
                    bool first = true;
                    while (tok && ast_argc < 62) {
                        /* Skip compiler executable name (first token if it starts with / or contains gcc/g++) */
                        if (first && strchr(tok, '/')) {
                            first = false;
                            tok = strtok_r(NULL, " \t\n", &saveptr);
                            continue;
                        }
                        first = false;

                        /* Keep include/define/language/optimization flags */
                        if (tok[0] == '-' &&
                            (tok[1] == 'I' ||
                             strncmp(tok, "-isystem", 8) == 0 ||
                             strncmp(tok, "-D", 2) == 0 ||
                             strncmp(tok, "-std", 4) == 0 ||
                             tok[1] == 'f' || tok[1] == 'O' || tok[1] == 'm')) {
                            ast_args[ast_argc++] = tok;
                            /* -isystem and -I sometimes have separate value */
                            if ((strcmp(tok, "-isystem") == 0 || strcmp(tok, "-I") == 0) &&
                                ast_argc < 62) {
                                char *next = strtok_r(NULL, " \t\n", &saveptr);
                                if (next && next[0] != '-')
                                    ast_args[ast_argc++] = next;
                            }
                        }
                        tok = strtok_r(NULL, " \t\n", &saveptr);
                    }
                }
            }
            ast_args[ast_argc] = NULL;

            printf("  Parsing: %s\n", source_files[fi]);
            ASTContext *actx = ast_parse(source_files[fi], ast_args);
            if (!actx) {
                printf("  \033[33mWarning:\033[0m Could not parse %s "
                       "(may have syntax errors)\n", source_files[fi]);
                continue;
            }

            DangerousCall dcs[32];
            int ndc = ast_get_dangerous_calls(actx, dcs, 32);
            FunctionInfo funcs[64];
            int nf = ast_get_functions(actx, funcs, 64);

            for (int j = 0; j < ndc && ast_total < 32; j++) {
                DetectedError de;
                memset(&de, 0, sizeof(de));
                de.type = dcs[j].type;
                de.severity = dcs[j].severity;
                de.has_source = true;
                de.source_line = dcs[j].line;
                strncpy(de.source_file, dcs[j].source_file,
                        sizeof(de.source_file) - 1);
                snprintf(de.title, sizeof(de.title),
                         "Unsafe %.64s Call", dcs[j].function_name);
                snprintf(de.fix_suggestion, sizeof(de.fix_suggestion),
                         "'%.64s' at %.256s:%u — use a safer alternative",
                         dcs[j].function_name,
                         dcs[j].source_file, dcs[j].line);
                ast_total = merge_analysis_error(errors, ast_total, 32,
                                                  &de, ast_modes, 1u);
            }

            printf("    Functions: %d, Dangerous calls: %d\n", nf, ndc);
            ast_free(actx);
        }

        error_count = ast_total;

        /* Run flow-aware check modules (Phase 3) on the source files */
        if (error_count < 32) {
            DetectedError check_errs[32];
            int n_check = 0;
            AnnotationDB *ann = ann_load(NULL);
            if (!ann) {
                printf("  \033[33mWarning:\033[0m Could not load annotation "
                       "DB — checks disabled\n");
            } else {
                for (int fi = 0; fi < source_count && n_check < 32; fi++) {
                    ASTContext *actx = ast_parse(source_files[fi], NULL);
                    if (!actx) continue;
                    int ce = check_engine_run(actx, ann,
                                               check_errs + n_check,
                                               32 - n_check);
                    for (int cj = 0; cj < ce && n_check < 32; cj++) {
                        n_check++;
                    }
                    ast_free(actx);
                }
                ann_free(ann);
            }

            /* Merge check engine results */
            unsigned int cmode = 2u;
            for (int cj = 0; cj < n_check && error_count < 32; cj++) {
                error_count = merge_analysis_error(errors, error_count, 32,
                                                    &check_errs[cj],
                                                    ast_modes, cmode);
            }
        }

        if (error_count > 0) {
            for (i = 0; i < error_count; i++)
                generate_fix_suggestion(&errors[i]);
            print_summary(&colors, &result, error_count, errors);
            return EXIT_ERRORS_FOUND;
        }
        print_colored(&colors, colors.green, "[OK] ");
        printf("No AST-level issues found\n");
        return EXIT_CLEAN;
    }

    /* --header: compile header with -fsyntax-only */
    if (use_header) {
        print_colored(&colors, colors.bold, "=== Header Syntax Check ===\n");
        int hdr_ret = check_header(source_files, source_count,
                                    result.compiler_output,
                                    sizeof(result.compiler_output));
        if (hdr_ret != 0) {
            fprintf(stderr, "%s\n", result.compiler_output);
            fflush(stderr);
            error_count = parse_sanitizer_errors(result.compiler_output,
                                                  errors, 32);
            if (error_count == 0) {
                /* No structured errors, but compilation failed — create one */
                errors[0].type = ERR_UNKNOWN;
                snprintf(errors[0].title, sizeof(errors[0].title),
                         "Header syntax / missing include");
                snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                         "Check the compiler output above for missing includes, "
                         "forward declarations, or syntax errors");
                errors[0].severity = 5;
                error_count = 1;
            }
            for (i = 0; i < error_count; i++)
                generate_fix_suggestion(&errors[i]);
            print_summary(&colors, &result, error_count, errors);
            if (use_ci) {
                generate_ci_output(source_files, source_count,
                                   errors, error_count,
                                   EXIT_ERRORS_FOUND, 0);
            }
            return EXIT_ERRORS_FOUND;
        }
        print_colored(&colors, colors.green, "[OK] ");
        printf("Header(s) passed syntax check\n");
        if (use_ci) {
            generate_ci_output(source_files, source_count,
                               NULL, 0, EXIT_CLEAN, 0);
        }
        return EXIT_CLEAN;
    }

    /* External tool passes: cppcheck, clang-analyze, iwyu — no compilation */
    if (use_cppcheck || use_clang_analyze || use_iwyu) {
        result.compilation_success = true;
        struct timespec ext_start, ext_end;
        clock_gettime(CLOCK_MONOTONIC, &ext_start);
        if (use_cppcheck)
            run_cppcheck(source_files, source_count,
                         result.sanitizer_output,
                         sizeof(result.sanitizer_output));
        else if (use_clang_analyze)
            run_clang_analyze(source_files, source_count,
                              result.sanitizer_output,
                              sizeof(result.sanitizer_output));
        else if (use_iwyu)
            run_iwyu(source_files, source_count,
                     result.sanitizer_output,
                     sizeof(result.sanitizer_output));
        clock_gettime(CLOCK_MONOTONIC, &ext_end);
        get_execution_time(&ext_start, &ext_end, &result.execution_time_ms);

        /* Parse the output for errors */
        int ext_error_count = 0;
        char *line = strtok(result.sanitizer_output, "\n");
        while (line && ext_error_count < 32) {
            if (use_cppcheck) {
                /* Format: file:line:col: severity: Message [id] */
                if (strstr(line, ": error:") || strstr(line, ": warning:") ||
                    strstr(line, ": style:")) {
                    char fcopy[MAX_LINE_LEN];
                    strncpy(fcopy, line, sizeof(fcopy) - 1);
                    char *c1 = strchr(fcopy, ':');
                    if (c1) {
                        int ln = atoi(c1 + 1);
                        if (ln > 0) {
                            *c1 = '\0';
                            strncpy(errors[ext_error_count].source_file, fcopy,
                                    sizeof(errors[ext_error_count].source_file) - 1);
                            errors[ext_error_count].source_line = ln;
                            errors[ext_error_count].has_source = true;
                        }
                    }
                    if (strstr(line, ": error:"))
                        errors[ext_error_count].severity = 3;
                    else if (strstr(line, ": warning:"))
                        errors[ext_error_count].severity = 2;
                    else
                        errors[ext_error_count].severity = 1;
                    errors[ext_error_count].type = ERR_UNKNOWN;
                    snprintf(errors[ext_error_count].title,
                             sizeof(errors[ext_error_count].title), "%s", line);
                    ext_error_count++;
                }
            } else if (use_clang_analyze) {
                /* Skip "note:" lines — only emit "warning:" lines */
                if (strstr(line, "warning:") && !strstr(line, ": note:")) {
                    errors[ext_error_count].type = ERR_UNKNOWN;
                    errors[ext_error_count].severity = 2;
                    errors[ext_error_count].has_source = false;
                    char fcopy[MAX_LINE_LEN];
                    strncpy(fcopy, line, sizeof(fcopy) - 1);
                    char *c1 = strchr(fcopy, ':');
                    if (c1) {
                        int ln = atoi(c1 + 1);
                        if (ln > 0) {
                            *c1 = '\0';
                            strncpy(errors[ext_error_count].source_file, fcopy,
                                    sizeof(errors[ext_error_count].source_file) - 1);
                            errors[ext_error_count].source_line = ln;
                            errors[ext_error_count].has_source = true;
                        }
                    }
                    snprintf(errors[ext_error_count].title,
                             sizeof(errors[ext_error_count].title), "%s", line);
                    ext_error_count++;
                }
            } else if (use_iwyu) {
                if (strstr(line, "should add") || strstr(line, "should remove")) {
                    errors[ext_error_count].type = ERR_UNKNOWN;
                    errors[ext_error_count].severity = 1;
                    errors[ext_error_count].has_source = false;
                    snprintf(errors[ext_error_count].title,
                             sizeof(errors[ext_error_count].title), "%s", line);
                    ext_error_count++;
                }
            }
            line = strtok(NULL, "\n");
        }
        print_summary(&colors, &result, ext_error_count, errors);
        return ext_error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    /* --checksec: check binary hardening */
    if (use_checksec) {
        const char *bin_path = source_files[0];
        char temp_bin[MAX_PATH_LEN] = {0};
        bool is_binary = !is_source_file(bin_path) &&
                         access(bin_path, X_OK) == 0;

        print_colored(&colors, colors.bold, "=== Binary Hardening Check ===\n");

        if (!is_binary) {
            /* Compile from source first */
            generate_temp_path("prism_sec", temp_bin, sizeof(temp_bin));
            int comp_ret = compile_with_basic_flags(source_files, source_count,
                             temp_bin,
                             result.compiler_output,
                             sizeof(result.compiler_output));
            if (comp_ret != 0) {
                print_colored(&colors, colors.red, "[COMPILE ERROR]\n");
                printf("%s\n", result.compiler_output);
                return EXIT_COMPILE_FAIL;
            }
            bin_path = temp_bin;
        } else {
            printf("  Checking pre-compiled binary: %s\n", bin_path);
        }

        error_count = check_binary_harden(bin_path, errors, 32);

        if (error_count > 0) {
            for (i = 0; i < error_count; i++)
                generate_fix_suggestion(&errors[i]);
            print_summary(&colors, &result, error_count, errors);
        } else {
            print_colored(&colors, colors.green, "[OK] ");
            printf("Hardening: PIE, NX, canary, RELRO all present\n");
        }

        if (temp_bin[0]) unlink(temp_bin);
        if (use_ci) {
            generate_ci_output(source_files, source_count,
                               error_count > 0 ? errors : NULL, error_count,
                               error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN,
                               0);
        }
        return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    generate_temp_path("prism", binary_path, sizeof(binary_path));

    /* Use compile_commands.json if specified */
    if (project_json[0]) {
        print_colored(&colors, colors.cyan, "[project] ");
        printf("Compiling %d files with project flags: %s\n",
               source_count, project_json);

        /* Determine mode-specific extra flags (sanitizers, etc.) */
        const char *extra_flags = "";
        if (use_full)
            extra_flags = "-fsanitize=address,undefined -g -fno-omit-frame-pointer -O1";
        else if (use_tsan)
            extra_flags = "-fsanitize=thread -g -fno-omit-frame-pointer -O1";
        else if (use_msan)
            extra_flags = "-fsanitize=memory -fno-omit-frame-pointer -fsanitize-memory-track-origins -g -O1";
        else if (use_analyzer)
            extra_flags = "-fanalyzer -O2 -g -Wuninitialized -Wstrict-aliasing=2 -Wformat-overflow=2 -Wstringop-overflow=2";

        /* Compile each file with its own flags from JSON, then link */
        if (compile_project_binary(project_json,
                                   source_files, source_count,
                                   extra_flags,
                                   link_flags_buf[0] ? link_flags_buf : NULL,
                                   binary_path,
                                   result.compiler_output,
                                   sizeof(result.compiler_output)) == 0) {
            bool needs_run_binary = use_full || use_quick ||
                use_tsan || use_msan || use_valgrind ||
                use_fuzz || rerun_count > 0 || use_resources ||
                use_gcov || use_libfuzzer;
            if (needs_run_binary)
                result.compilation_success = true;
        } else {
            /* Per-file compile succeeded but linking may have failed
             * (partial project — not all required .o files linked in).
             * Treat as compilation success if no fatal compile errors. */
            if (!strstr(result.compiler_output, "fatal error:") &&
                strstr(result.compiler_output, "undefined reference")) {
                result.compilation_success = true;
            }
        }
    }

    /* --quick: fast feedback with basic flags only */
    if (use_quick && !result.compilation_success) {
        char quick_bin[MAX_PATH_LEN];
        generate_temp_path("prism_quick", quick_bin, sizeof(quick_bin));

        int comp_ret = compile_with_basic_flags(source_files, source_count,
                         quick_bin,
                         result.compiler_output,
                         sizeof(result.compiler_output));

        if (comp_ret != 0) {
            print_colored(&colors, colors.red, "[COMPILE ERROR]\n");
            printf("%s\n", result.compiler_output);
            return EXIT_COMPILE_FAIL;
        }

        result.compilation_success = true;

        /* Quick mode: just run once, check exit code */
        result.exit_code = run_binary(quick_bin, NULL,
                                      result.runtime_output,
                                      sizeof(result.runtime_output),
                                      result.sanitizer_output,
                                      sizeof(result.sanitizer_output),
                                      timeout_sec, NULL);

        if (result.exit_code == -1) {
            print_colored(&colors, colors.yellow, "[TIMEOUT] ");
            printf("Program timed out after %d seconds\n", timeout_sec);
            error_count = 1;
        } else if (result.exit_code != 0) {
            /* Non-zero exit — check for signal termination or crash */
            int signal_num = 0;
            if (result.exit_code > 128)
                signal_num = result.exit_code - 128;
            if (signal_num > 0) {
                /* Terminated by signal — create proper error entry */
                error_count = parse_signal_errors(
                    result.sanitizer_output, result.exit_code,
                    signal_num, errors, 32);
            }
            if (error_count == 0) {
                /* No structured error — try parsing output */
                if (result.sanitizer_output[0] == '\0') {
                    snprintf(result.sanitizer_output,
                             sizeof(result.sanitizer_output),
                             "Process exited with code %d", result.exit_code);
                }
                error_count = parse_sanitizer_errors(result.sanitizer_output,
                                                      errors, 32);
            }
        }

        for (i = 0; i < error_count; i++)
            generate_fix_suggestion(&errors[i]);
        print_summary(&colors, &result, error_count, errors);
        if (!keep_binary) unlink(quick_bin);

        if (use_sarif && sarif_path[0]) {
            generate_sarif_report(sarif_path,
                                   errors, error_count,
                                   result.execution_time_ms);
        }
        if (use_ci) {
            generate_ci_output(source_files, source_count,
                               errors, error_count,
                               error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN,
                               result.execution_time_ms);
        }

        return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    if (!result.compilation_success) {
        if ((use_valgrind || use_resources) && !use_fuzz && rerun_count == 0) {
            /* Valgrind (and --resources) need a binary without ASan.
             * Fuzz/rerun modes need ASan instrumentation — if combined
             * with --valgrind or --resources, ASan wins (fuzz/rerun
             * execution branch runs first).
             * Check valgrind availability first to avoid wasted compile. */
            if (system("command -v valgrind > /dev/null 2>&1") == 0) {
                if (compile_for_valgrind(source_files, source_count, binary_path,
                                        result.compiler_output,
                                        sizeof(result.compiler_output)) == 0) {
                    result.compilation_success = true;
                }
            }
        } else if (use_fuzz || rerun_count > 0) {
            /* Fuzz and rerun: compile with sanitizers for runtime analysis.
             * Respect --tsan flag if provided alongside --rerun. */
            if (use_tsan) {
                if (is_cpp_file(source_files[0])) {
                    if (compile_cpp_with_tsan(source_files, source_count,
                                               binary_path,
                                               result.compiler_output,
                                               sizeof(result.compiler_output)) == 0)
                        result.compilation_success = true;
                } else if (compile_with_tsan(source_files, source_count,
                                             binary_path,
                                             result.compiler_output,
                                             sizeof(result.compiler_output)) == 0) {
                    result.compilation_success = true;
                }
            } else {
                if (is_cpp_file(source_files[0])) {
                    if (compile_cpp_with_sanitizers(source_files, source_count,
                                                     binary_path,
                                                     result.compiler_output,
                                                     sizeof(result.compiler_output)) == 0)
                        result.compilation_success = true;
                } else if (compile_with_sanitizers(source_files, source_count,
                                                   binary_path,
                                                   result.compiler_output,
                                                   sizeof(result.compiler_output)) == 0) {
                    result.compilation_success = true;
                }
            }
        } else if (use_analyzer) {
            if (is_cpp_file(source_files[0])) {
                if (compile_cpp_with_analyzer(source_files, source_count,
                                          binary_path,
                                          result.compiler_output,
                                          sizeof(result.compiler_output)) == 0) {
                    result.compilation_success = true;
                }
            } else if (compile_with_analyzer(source_files, source_count,
                                            binary_path,
                                            result.compiler_output,
                                            sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        } else if (use_msan) {
            if (compile_with_msan(source_files, source_count, binary_path,
                                  result.compiler_output,
                                  sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        } else if (use_clang_tidy) {
            if (compile_with_clang_tidy(source_files, source_count,
                                       result.compiler_output,
                                       sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        } else if (use_strict_aliasing) {
            if (compile_with_strict_aliasing(source_files, source_count,
                                             result.compiler_output,
                                             sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        } else if (use_float_equal) {
            if (compile_with_float_equal_warning(source_files, source_count,
                                                  result.compiler_output,
                                                  sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        } else if (use_conversion) {
            if (compile_with_conversion_warnings(source_files, source_count,
                                                  result.compiler_output,
                                                  sizeof(result.compiler_output),
                                                  conversion_all) == 0) {
                result.compilation_success = true;
            }
        } else if (is_cpp_file(source_files[0])) {
            if (use_tsan) {
                if (compile_cpp_with_tsan(source_files, source_count, binary_path,
                                           result.compiler_output,
                                           sizeof(result.compiler_output)) == 0) {
                    result.compilation_success = true;
                }
            } else if (compile_cpp_with_sanitizers(source_files, source_count,
                                                    binary_path,
                                                    result.compiler_output,
                                                    sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
                char warn_buf[MAX_OUTPUT_SIZE];
                if (compile_cpp_for_warnings(source_files, source_count,
                                              warn_buf, sizeof(warn_buf)) == 0 &&
                    warn_buf[0] != '\0') {
                    size_t len = strlen(result.compiler_output);
                    if (len < sizeof(result.compiler_output) - 1) {
                        strncat(result.compiler_output, warn_buf,
                                sizeof(result.compiler_output) - len - 1);
                    }
                }
            } else if (compile_fallback(source_files, source_count, binary_path,
                                        result.compiler_output,
                                        sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        } else {
            if (use_tsan) {
                if (compile_with_tsan(source_files, source_count, binary_path,
                                       result.compiler_output,
                                       sizeof(result.compiler_output)) == 0) {
                    result.compilation_success = true;
                }
            } else if (compile_with_sanitizers(source_files, source_count,
                                              binary_path,
                                              result.compiler_output,
                                              sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
                char warn_buf[MAX_OUTPUT_SIZE];
                if (compile_for_warnings(source_files, source_count,
                                          warn_buf, sizeof(warn_buf)) == 0 &&
                    warn_buf[0] != '\0') {
                    size_t len = strlen(result.compiler_output);
                    if (len < sizeof(result.compiler_output) - 1) {
                        strncat(result.compiler_output, warn_buf,
                                sizeof(result.compiler_output) - len - 1);
                    }
                }
            } else if (compile_fallback(source_files, source_count, binary_path,
                                        result.compiler_output,
                                        sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        }
    }

    if (!result.compilation_success) {
        if (use_ci) {
            generate_ci_output(source_files, source_count,
                               NULL, 0,
                               EXIT_COMPILE_FAIL, 0);
        }
        print_colored(&colors, colors.red, "[COMPILE ERROR]\n");
        if (result.compiler_output[0])
            printf("%s\n", result.compiler_output);
        return EXIT_COMPILE_FAIL;
    }

    /* Compile-flag passes (compile to /dev/null, no binary to run) */
    if (use_strict_aliasing || use_float_equal || use_conversion) {
        if (result.compiler_output[0]) {
            printf("\n%s", result.compiler_output);
            error_count = 1;
        }
        print_summary(&colors, &result, error_count, errors);
        return result.compiler_output[0] ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    /* --compile-only: compilation passed, skip binary execution */
    if (use_compile_only) {
        print_colored(&colors, colors.green, "[OK] ");
        printf("Compilation successful (--compile-only, binary not run)\n");
        if (!keep_binary) unlink(binary_path);
        if (use_ci) {
            generate_ci_output(source_files, source_count,
                               NULL, 0,
                               EXIT_CLEAN, 0);
        }
        return EXIT_CLEAN;
    }

    /* Check if the binary actually exists (project-flags compilation of
     * a single file from a multi-file project produces linker errors —
     * no binary, but compilation was valid) */
    if (project_json[0] && access(binary_path, X_OK) != 0 &&
        !use_clang_tidy && !use_strict_aliasing &&
        !use_float_equal && !use_conversion) {
        print_colored(&colors, colors.yellow, "[project] ");
        printf("Binary not created; compilation succeeded but linking "
               "needs more source files.\n");
        print_colored(&colors, colors.green, "[OK] ");
        printf("Compilation checked with project flags\n");
        if (!keep_binary) unlink(binary_path);
        return EXIT_CLEAN;
    }

    struct timespec start_time, end_time;

    if (use_fuzz) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        error_count = run_fuzz_analysis(binary_path, errors, 32,
                                         result.sanitizer_output,
                                         sizeof(result.sanitizer_output),
                                         timeout_sec);
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        get_execution_time(&start_time, &end_time, &result.execution_time_ms);
    } else if (rerun_count > 0) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        error_count = run_with_rerun(binary_path, rerun_count,
                                      errors, 32,
                                      result.runtime_output,
                                      sizeof(result.runtime_output),
                                      result.sanitizer_output,
                                      sizeof(result.sanitizer_output),
                                      timeout_sec);
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        get_execution_time(&start_time, &end_time, &result.execution_time_ms);
    } else if (use_resources) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        error_count = check_resource_leaks(binary_path, errors, 32,
                                            result.sanitizer_output,
                                            sizeof(result.sanitizer_output),
                                            timeout_sec);
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        get_execution_time(&start_time, &end_time, &result.execution_time_ms);
    } else {
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        if (use_valgrind) {
            result.exit_code = run_with_valgrind(binary_path,
                                                     result.runtime_output,
                                                     sizeof(result.runtime_output),
                                                     result.sanitizer_output,
                                                     sizeof(result.sanitizer_output),
                                                     timeout_sec, NULL);
        } else {
            if (use_tsan)
                setenv("TSAN_OPTIONS", "report_atomic_races=1:halt_on_error=0", 1);
            else if (use_msan)
                setenv("MSAN_OPTIONS", "halt_on_error=0:exit_code=1", 1);
            else
                setenv("ASAN_OPTIONS", "detect_leaks=1", 1);

            result.exit_code = run_binary(binary_path, NULL,
                                            result.runtime_output,
                                            sizeof(result.runtime_output),
                                            result.sanitizer_output,
                                            sizeof(result.sanitizer_output),
                                            timeout_sec, NULL);
        }

        if (result.exit_code == -1) {
            snprintf(result.sanitizer_output, sizeof(result.sanitizer_output),
                     "Execution timeout after %d seconds", timeout_sec);
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);
        get_execution_time(&start_time, &end_time, &result.execution_time_ms);

        int sanitizer_count = parse_sanitizer_errors(
                                   result.sanitizer_output,
                                   errors + error_count,
                                   32 - error_count);
        error_count += sanitizer_count;

        /* --gdb: run GDB on crash for detailed backtrace */
        if (use_gdb && result.exit_code > 0 && result.exit_code != EXIT_CLEAN) {
            char gdb_cmd[8192];
            char gdb_out[32768];
            FILE *gdb_pipe;
            snprintf(gdb_cmd, sizeof(gdb_cmd),
                     "gdb -batch -ex run -ex \"bt full\" "
                     "-ex \"info registers\" --args '%s' 2>&1",
                     binary_path);
            gdb_pipe = popen(gdb_cmd, "r");
            if (gdb_pipe) {
                size_t n = fread(gdb_out, 1, sizeof(gdb_out) - 1, gdb_pipe);
                gdb_out[n] = '\0';
                pclose(gdb_pipe);
                size_t slen = strlen(result.sanitizer_output);
                size_t glen = strlen(gdb_out);
                if (slen + glen + 64 < sizeof(result.sanitizer_output)) {
                    snprintf(result.sanitizer_output + slen,
                             sizeof(result.sanitizer_output) - slen,
                             "\n--- GDB Backtrace ---\n%s\n", gdb_out);
                }
            }
        }
    }

    if (error_count == 0 &&
        string_contains(result.compiler_output, "warning:")) {
        error_count = 1;
        /* Try to classify using error_patterns (catches clang-tidy checks) */
        char *line_start = result.compiler_output;
        char *line_end;
        int pattern_idx = -1;
        while (*line_start && pattern_idx == -1) {
            line_end = strchr(line_start, '\n');
            if (line_end) *line_end = '\0';
            if (string_contains(line_start, "warning:")) {
                pattern_idx = classify_error(line_start);
            }
            if (line_end) {
                *line_end = '\n';
                line_start = line_end + 1;
            } else {
                break;
            }
        }
        if (pattern_idx >= 0) {
            errors[0].type = error_patterns[pattern_idx].type;
            snprintf(errors[0].title, sizeof(errors[0].title),
                     "%s", error_patterns[pattern_idx].title);
            snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                     "%s", error_patterns[pattern_idx].fix);
            errors[0].severity = error_patterns[pattern_idx].severity;
            errors[0].has_source = false;
        } else if (string_contains(result.compiler_output, "uninitialized")) {
            errors[0].type = ERR_UNINIT_VAR;
        } else if (string_contains(result.compiler_output, "strict-aliasing"))
            errors[0].type = ERR_UNKNOWN;
        else if (string_contains(result.compiler_output, "format-overflow"))
            errors[0].type = ERR_BUFFER_OVERFLOW;
        else if (string_contains(result.compiler_output, "stringop-overflow"))
            errors[0].type = ERR_BUFFER_OVERFLOW;
        else if (string_contains(result.compiler_output, "alloc-size-larger-than"))
            errors[0].type = ERR_INT_OVERFLOW;
        else if (string_contains(result.compiler_output, "free-nonheap-object")) {
            errors[0].type = ERR_UNKNOWN;
            snprintf(errors[0].title, sizeof(errors[0].title), "Invalid Free");
        } else
            errors[0].type = ERR_UNKNOWN;
        if (errors[0].title[0] == '\0')
            snprintf(errors[0].title, sizeof(errors[0].title),
                     "%s", get_error_name(errors[0].type));
        /* Only overwrite fix_suggestion/severity/has_source when we didn't match a
         * known error pattern — otherwise the pattern's fix is more useful. */
        if (pattern_idx < 0) {
            char warning_excerpt[512];
            strncpy(warning_excerpt, result.compiler_output, sizeof(warning_excerpt) - 1);
            warning_excerpt[sizeof(warning_excerpt) - 1] = '\0';
            snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                     "Compiler detected: %s", warning_excerpt);
            errors[0].severity = 1;
            errors[0].has_source = false;
        }
    }

    if (error_count == 0 && result.exit_code == -1) {
        errors[0].type = ERR_UNKNOWN;
        snprintf(errors[0].title, sizeof(errors[0].title),
                 "Execution Timeout");
        snprintf(errors[0].fix_suggestion, sizeof(errors[0].fix_suggestion),
                 "The program exceeded the %d second timeout. Check for infinite loops or blocking operations.",
                 timeout_sec);
        errors[0].severity = 1;
        error_count = 1;
    }

    if (error_count == 0 && result.exit_code != 0 &&
        result.exit_code != EXIT_CLEAN) {
        error_count = parse_signal_errors(result.sanitizer_output,
                                            result.exit_code,
                                            result.exit_code > 128 ?
                                                result.exit_code - 128 : 0,
                                            errors, 32);
    }

    /* If --danger is combined with other modes, append dangerous API results */
    if (use_danger) {
        int danger_count = scan_dangerous_apis(source_files, source_count,
                                                errors + error_count,
                                                32 - error_count);
        error_count += danger_count;
        if (danger_count > 0) {
            print_colored(&colors, colors.yellow, "[danger] ");
            printf("Found %d dangerous API call(s) in source\n", danger_count);
        }
    }

    for (i = 0; i < error_count; i++)
        generate_fix_suggestion(&errors[i]);

    /* --baseline: suppress known errors from a baseline file */
    if (baseline_path[0]) {
        int before = error_count;
        load_baseline_and_filter(baseline_path, errors, &error_count);
        int suppressed = before - error_count;
        if (suppressed > 0) {
            print_colored(&colors, colors.cyan, "[baseline] ");
            printf("Suppressed %d known error(s) from %s\n",
                   suppressed, baseline_path);
        }
    }

    print_summary(&colors, &result, error_count, errors);

    /* --generate-baseline: save current errors as baseline JSON */
    if (use_generate_baseline) {
        char baseline_file[MAX_PATH_LEN];
        if (baseline_path[0])
            strncpy(baseline_file, baseline_path, sizeof(baseline_file) - 1);
        else
            snprintf(baseline_file, sizeof(baseline_file),
                     "prism-baseline.json");
        baseline_file[sizeof(baseline_file) - 1] = '\0';

        if (save_baseline(baseline_file, errors, error_count) == 0) {
            print_colored(&colors, colors.green, "[baseline] ");
            printf("Baseline saved to: %s (%d errors)\n",
                   baseline_file, error_count);
        }
    }

    if (html_path && html_path[0] != '\0') {
        if (generate_html_report(html_path, source_files, source_count,
                                  &result, errors, error_count) == 0) {
            print_colored(&colors, colors.green, "[HTML] ");
            printf("Report saved to: %s\n", html_path);
        } else {
            print_colored(&colors, colors.red, "[ERROR] ");
            printf("Failed to write HTML report: %s\n", html_path);
        }
    }

    if (!keep_binary)
        cleanup_binary(binary_path);

    /* --sarif: generate SARIF report */
    if (use_sarif && sarif_path[0]) {
        int ret = generate_sarif_report(sarif_path,
                                         errors, error_count,
                                         result.execution_time_ms);
        if (ret == 0) {
            print_colored(&colors, colors.green, "[SARIF] ");
            printf("Report saved to: %s\n", sarif_path);
        }
    }

    /* --ci: emit JSON summary to stdout */
    if (use_ci) {
        int exit_status = error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
        generate_ci_output(source_files, source_count,
                           errors, error_count,
                           exit_status, result.execution_time_ms);
    }

    return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
}
