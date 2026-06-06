/*
 * c_tester.c - Core engine for c_tester, a simple C/C++ error detection tool
 *
 * Purpose: Compiles C/C++ source files with GCC/G++ sanitizers (ASan/UBSan),
 *          runs them with timeout, captures output, and provides the
 *          foundation for error detection and fix suggestions.
 *
 * Design: Uses popen() for compilation output capture, fork()/execvp()/select()
 *         for binary execution with timeout. Falls back to non-sanitizer
 *         compilation when sanitizer libraries are unavailable.
 *         Auto-detects language from file extension (.c vs .cpp/.cxx/.cc).
 *
 * Thread-safety: Single-threaded tool, no concurrency concerns.
 */

#include "c_tester.h"

/*
 * compile_with_sanitizers - Compile source files with ASan and UBSan
 *
 * WHY: Sanitizers catch runtime errors at execution time that compilers
 *      miss. ASan catches memory errors, UBSan catches undefined behavior.
 *      Supports multiple source files compiled into a single binary.
 *
 * @param sources - Array of paths to C source files
 * @param source_count - Number of source files
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 *
 * WHY: TSan detects data races and thread synchronization bugs at runtime.
 *      ASan and TSan are mutually exclusive, so we use a separate function.
 *      -fno-omit-frame-pointer is required for useful stack traces.
 *      Supports multiple source files compiled into a single binary.
 *
 * @param sources - Array of paths to C source files
 * @param source_count - Number of source files
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 *
 * WHY: Some systems lack sanitizer libraries. Fallback to basic
 *      compilation with -Wall -Wextra for basic error detection.
 *      Supports multiple source files compiled into a single binary.
 *
 * @param sources - Array of paths to C source files
 * @param source_count - Number of source files
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 *
 * WHY: Valgrind needs unoptimized code (-Og) to detect memory bugs.
 *      -O2 optimization can eliminate dead code (unused malloc results),
 *      making memory leaks invisible to Valgrind.
 *
 * @param sources - Array of paths to C source files
 * @param source_count - Number of source files
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 *
 * WHY: Sanitizers suppress certain warnings (e.g., strict-aliasing).
 *      A separate warning-only pass catches these at -O2 optimization.
 *      Supports multiple source files.
 *
 * @param sources - Array of paths to C source files
 * @param source_count - Number of source files
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 *
 * WHY: Same as compile_with_sanitizers but uses g++ for C++ sources.
 *      C++ sanitizer output may include std::error messages.
 *      Supports multiple source files compiled into a single binary.
 *
 * @param sources - Array of paths to C++ source files
 * @param source_count - Number of source files
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 *
 * WHY: TSan detects data races in C++ programs.
 *      ASan and TSan are mutually exclusive.
 *      Supports multiple source files compiled into a single binary.
 *
 * @param sources - Array of paths to C++ source files
 * @param source_count - Number of source files
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
                   "g++ -fsanitize=thread -g -fno-omit-frame-pointer "
                   "-o '%s'", binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
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
 *
 * WHY: GCC 13+ has a built-in static analyzer that performs symbolic
 *      data flow analysis without running the code. Catches leaks,
 *      use-after-free, double-free, NULL deref, buffer overflows
 *      at compile time. Requires no runtime execution.
 *
 * @param sources - Array of paths to C source files
 * @param source_count - Number of source files
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 *
 * WHY: clang-tidy performs static analysis without compiling or running
 *      the code. It catches bugprone patterns, concurrency issues,
 *      cert security issues, and clang-analyzer checks at source level.
 *      Unlike -fanalyzer, it has a broader set of checks.
 *
 * @param sources - Array of paths to C/C++ source files
 * @param source_count - Number of source files
 * @param output - Buffer for clang-tidy output
 * @param output_size - Size of output buffer
 * @return 0 if clang-tidy ran successfully, non-zero on failure
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
 * compile_cpp_for_warnings - Compile C++ with warning flags only
 *
 * WHY: Catch C++-specific warnings that sanitizers might suppress.
 *      Supports multiple source files.
 *
 * @param sources - Array of paths to C++ source files
 * @param source_count - Number of source files
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
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
 * WHY: C++ version of compile_with_analyzer using g++.
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
 * run_with_timeout - Shared helper: pipe/fork/execvp/select/read
 *
 * WHY: DRY up run_binary() and run_with_valgrind() which share
 *      identical pipe, fork, select-loop, and read logic.
 *      Caller builds argv[] and passes it; this function does the rest.
 *
 * @param binary - Path to binary (first arg to execvp)
 * @param argv - Full argument vector (including argv[0])
 * @param output - Buffer for stdout
 * @param output_size - Size of output buffer
 * @param error_output - Buffer for stderr
 * @param error_size - Size of error buffer
 * @param timeout_sec - Maximum execution time in seconds
 * @param child_pid - Output: PID of child process
 * @return Exit code of child, 128+sig if signaled, or -1 on failure/timeout
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
        /* Child: redirect stdout/stderr to pipes, then exec */
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
            waitpid(pid, &status, 0);
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
        if (ready < 0)
            break;

        if (ready == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
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

    waitpid(pid, &status, 0);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return -1;
}

/*
 * run_binary - Execute compiled binary with timeout
 *
 * WHY: Need to run test with timeout to catch infinite loops
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
 *
 * WHY: Valgrind catches memory errors that sanitizers might miss,
 *      particularly uninitialized reads and subtle memory leaks.
 *      Runs much slower (10-50x) but provides complementary coverage.
 *
 * @param binary - Path to binary to execute under Valgrind
 * @param output - Buffer for runtime output (stdout)
 * @param output_size - Size of output buffer
 * @param error_output - Buffer for Valgrind output (stderr)
 * @param error_size - Size of error buffer
 * @param timeout_sec - Maximum execution time in seconds
 * @param child_pid - Output: PID of child process
 * @return Exit code of Valgrind/binary, or -1 on failure/timeout
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
 * WHY: Need to create temporary binary files for each test
 *      without conflicting with existing files or other tests.
 */
char *generate_temp_path(const char *prefix, char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, "/tmp/c_tester_%s_XXXXXX", prefix);
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
 *
 * WHY: Only files with main() can be compiled and executed.
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
        if (string_contains(line, "int main") ||
            string_contains(line, "main(")) {
            found = true;
            break;
        }
    }

    fclose(fp);
    return found;
}

/* Check if haystack contains needle (case-sensitive) */
bool string_contains(const char *haystack, const char *needle)
{
    return haystack && needle && strstr(haystack, needle) != NULL;
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
 * WHY: Need to track how long a test took to execute,
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
 *
 * WHY: Sanitizer output contains specific keywords that identify
 *      the type of error (e.g., "heap-buffer-overflow").
 *      Returns pattern index so caller gets correct title/fix.
 *
 * @param error_line - Single line from sanitizer output
 * @return Pattern index (0..num_patterns-1), or -1 if no match
 */
int classify_error(const char *error_line)
{
    int i;

    if (!error_line)
        return -1;

    for (i = 0; i < num_patterns; i++) {
        if (string_contains(error_line, error_patterns[i].pattern))
            return i;
    }

    return -1;
}

/*
 * parse_sanitizer_errors - Extract errors from sanitizer output
 *
 * WHY: Sanitizer output is multi-line with specific format.
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

            snprintf(errors[count].title, sizeof(errors[count].title),
                     "%s", error_patterns[pattern_idx].title);
            snprintf(errors[count].fix_suggestion,
                     sizeof(errors[count].fix_suggestion),
                     "%s", error_patterns[pattern_idx].fix);

            /* Try to extract file:line from nearby lines */
            const char *file_ref = strstr(line, ".c:");
            if (file_ref) {
                const char *path_start = file_ref;
                while (path_start > line && *(path_start - 1) != ' ' &&
                       *(path_start - 1) != '\t')
                    path_start--;
                size_t file_len = file_ref - path_start + 2;
                if (file_len < MAX_PATH_LEN - 1) {
                    memcpy(errors[count].source_file, path_start, file_len);
                    errors[count].source_file[file_len] = '\0';
                    errors[count].has_source = true;
                }
                errors[count].source_line = atoi(file_ref + 3);
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
 * WHY: When a program crashes with a signal (SIGSEGV, SIGABRT, etc.),
 *      we can classify the error based on the signal received.
 */
int parse_signal_errors(const char *error_output, int exit_code,
                        int signal, DetectedError *errors, int max_errors)
{
    (void)error_output;
    if (!errors || max_errors <= 0)
        return 0;

    if (signal == SIGSEGV || exit_code == 139) {
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
 * WHY: The tool should be helpful by suggesting how to fix
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
    case ERR_UNKNOWN:         return "Unknown Error";
    case ERR_NONE:            return "No Error";
    }
    return "Unknown Error";
}

/*
 * get_source_line - Read a specific line from source file
 *
 * WHY: When reporting errors, showing the offending line of code
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
 * WHY: Terminal colors help distinguish error types visually.
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
 * WHY: Color-coded output makes errors stand out and improves
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
 * WHY: Visual header helps user identify which file(s) are being
 *      tested, especially when running multiple tests.
 *      Shows file count when multiple files are provided.
 */
void print_banner(const ColorCodes *colors, const char **source_files, int source_count)
{
    int i;

    if (!colors || !source_files || source_count <= 0)
        return;

    printf("========================================\n");
    print_colored(colors, colors->bold, "  c_tester - C Error Detection Tool\n");
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
 * WHY: Summarize findings for the user with clear pass/fail
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
        for (i = 0; i < error_count; i++) {
            print_colored(colors, colors->red, "[ERROR] ");
            print_colored(colors, colors->bold, "%s", errors[i].title);
            if (errors[i].has_source && errors[i].source_line > 0)
                printf(" (%s:%d)", errors[i].source_file,
                       errors[i].source_line);
            printf("\n");

            print_colored(colors, colors->yellow, "  Fix: ");
            printf("%s\n", errors[i].fix_suggestion);

            if (errors[i].has_source && errors[i].source_line > 0 &&
                get_source_line(errors[i].source_file, errors[i].source_line,
                                source_line, sizeof(source_line)) == 0) {
                print_colored(colors, colors->cyan, "  -> %d | ",
                              errors[i].source_line);
                printf("%s\n", source_line);
            }
            printf("\n");
        }
        print_colored(colors, colors->red, "[ERROR] ");
        printf("%d error(s) detected\n", error_count);
    } else if (result->compilation_success) {
        print_colored(colors, colors->green, "[OK] ");
        print_colored(colors, colors->bold,
                      "No errors detected - clean run in %ldms\n",
                      result->execution_time_ms);
    } else {
        print_colored(colors, colors->red, "[COMPILE ERROR]\n");
        if (result->compiler_output[0])
            printf("%s\n", result->compiler_output);
    }
}

/*
 * generate_html_report - Write HTML report with error details
 *
 * WHY: Users need a shareable, styled report that shows errors,
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
    fprintf(fp, "  <title>c_tester Report - %s</title>\n", primary_source);
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
    fprintf(fp, "  <h1>c_tester Report</h1>\n");
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
 *
 * WHY: Multiple analysis passes may find the same error or different errors
 *      of the same type. Dedup by (type + file + line) so distinct errors
 *      at different locations are all reported, while the same error found
 *      by multiple modes is merged with combined mode tracking.
 *
 * @param errors - Global error array
 * @param total_errors - Current count in errors[]
 * @param max_errors - Capacity of errors[]
 * @param new_error - The candidate error to add
 * @param error_modes - Mode bitmask array (parallel to errors[])
 * @param mode_bit - Bit for the current analysis mode
 * @return New total_errors count
 */
static int merge_analysis_error(DetectedError *errors, int total_errors, int max_errors,
                                 const DetectedError *new_error,
                                 unsigned int *error_modes, unsigned int mode_bit)
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
 *
 * WHY: Provides comprehensive analysis by running every detection mode
 *      (sanitizers, warnings, analyzer, clang-tidy, valgrind, tsan)
 *      and aggregating all unique errors with deduplication.
 *
 * @param sources - Array of paths to source files
 * @param source_count - Number of source files
 * @param errors - Output array for detected errors
 * @param max_errors - Maximum number of errors to collect
 * @param result - TestResult to populate with execution info
 * @param timeout_sec - Maximum execution time in seconds
 * @param colors - Color codes for output
 * @return Number of unique errors found
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
    unsigned int error_modes[32] = {0};
    long cumulative_time_ms = 0;

    generate_temp_path("c_tester", binary_path, sizeof(binary_path));
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
    if (system("which valgrind > /dev/null 2>&1") != 0) {
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
 *
 * WHY: Users need clear documentation of available options and examples
 *      to use the tool effectively. Grouped options improve readability.
 *
 * @param prog_name - Program name from argv[0]
 * @param colors - Color codes for colored output
 */
void print_usage(const char *prog_name, const ColorCodes *colors)
{
    print_colored(colors, colors->bold, "c_tester - C Error Detection Tool\n");
    printf("Detects memory errors, undefined behavior, and bugs in C/C++ code.\n\n");

    print_colored(colors, colors->bold, "Usage: ");
    printf("%s [options] <source.c> [source2.c ...]\n\n", prog_name);

    print_colored(colors, colors->bold, "Analysis:\n");
    printf("  --max          Run ALL analysis modes (slow but thorough)\n");
    printf("  --tsan         Use ThreadSanitizer to detect data races\n");
    printf("  --analyzer     Use GCC static analyzer (-fanalyzer)\n");
    printf("  --clang-tidy   Run clang-tidy static analysis\n");
    printf("  --valgrind     Run under Valgrind for deep memory analysis\n");
    printf("  --project=<file>   Use compile flags from compile_commands.json\n\n");

    print_colored(colors, colors->bold, "Output:\n");
    printf("  --html=<path>  Generate HTML report at specified path\n");
    printf("  --no-color     Disable colored output\n\n");

    print_colored(colors, colors->bold, "Execution:\n");
    printf("  --keep         Keep compiled binary after run\n");
    printf("  --timeout=N    Set execution timeout in seconds (default: %d)\n",
           DEFAULT_TIMEOUT_SEC);
    printf("  --jobs=N, -j N Process multiple files in parallel (max: 8)\n\n");

    print_colored(colors, colors->bold, "Examples:\n");
    printf("  %s main.c                          Basic error detection\n", prog_name);
    printf("  %s --tsan main.c                   Detect data races\n", prog_name);
    printf("  %s --valgrind main.c               Deep memory analysis\n", prog_name);
    printf("  %s --html=report.html main.c       Generate HTML report\n", prog_name);
    printf("  %s main.c utils.c helper.c         Multi-file project\n\n", prog_name);

     printf("Supported files: .c, .cpp, .cxx, .cc\n");
}

/*
 * parse_compile_commands - Extract compile flags for a source file
 *
 * WHY: compile_commands.json is a standard format (CMake, Bear, etc.)
 *      Parse it to get the actual compile flags used in the project.
 *
 * @param json_path - Path to compile_commands.json
 * @param source_file - Source file to find flags for
 * @param flags_output - Output: extracted flags string
 * @param flags_size - Size of flags buffer
 * @return 0 on success, -1 on error
 */
int parse_compile_commands(const char *json_path, const char *source_file,
                          char *flags_output, size_t flags_size)
{
    char cmd[4096];
    FILE *pipe;
    size_t bytes_read;

    /* Check if jq is available */
    if (system("which jq > /dev/null 2>&1") != 0) {
        snprintf(flags_output, flags_size, "jq not found. Install jq to use compile_commands.json");
        return -1;
    }

    /* Use jq to extract the command for this source file */
    snprintf(cmd, sizeof(cmd),
             "jq -r '.[] | select(.file | endswith(\"%s\")) | .command' '%s' 2>/dev/null",
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
 * run_with_compile_flags - Compile using flags from compile_commands.json
 *
 * WHY: Use the exact flags from the project's build system
 *      instead of guessing. This ensures accurate analysis.
 *
 * @param sources - Array of source files
 * @param source_count - Number of source files
 * @param flags - Flags string from compile_commands.json
 * @param binary - Output binary path
 * @param output - Buffer for compiler output
 * @param output_size - Size of output buffer
 * @return 0 on success, non-zero on failure
 */
int run_with_compile_flags(const char **sources, int source_count,
                           const char *flags,
                           const char *binary,
                           char *output, size_t output_size)
{
    char cmd[MAX_PATH_LEN * 8 + 1024];
    FILE *pipe;
    size_t bytes_read;
    int i;
    size_t pos;

    pos = snprintf(cmd, sizeof(cmd), "%s -o '%s'", flags, binary);

    for (i = 0; i < source_count && pos < sizeof(cmd) - 4; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " '%s'", sources[i]);
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
 * main - CLI entry point for c_tester
 *
 * WHY: Parse command-line arguments, orchestrate the compilation,
 *      execution, and error reporting workflow.
 *      Supports multiple source files compiled into a single binary.
 */
int main(int argc, char *argv[])
{
    ColorCodes colors;
    TestResult result;
    DetectedError errors[32];
    memset(errors, 0, sizeof(errors));
    char binary_path[MAX_PATH_LEN];
    const char *source_files[32];
    int source_count = 0;
    bool keep_binary = false;
    bool use_color = true;
    bool use_tsan = false;
    bool use_analyzer = false;
    bool use_clang_tidy = false;
    bool use_valgrind = false;
    bool use_max = false;
    char project_json[MAX_PATH_LEN] = {0};
    const char *html_path = NULL;
    int timeout_sec = DEFAULT_TIMEOUT_SEC;
    int jobs = 1;
    int error_count = 0;
    int i;

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
            /* Matched --jobs= at argv[i] */
            jobs = atoi(argv[i] + 8);
            /* After parsing --jobs= */
            if (jobs < 1) jobs = 1;
            if (jobs > 8) jobs = 8;
        } else if (strcmp(argv[i], "--jobs") == 0 && i + 1 < argc) {
            jobs = atoi(argv[++i]);
            if (jobs < 1) jobs = 1;
            if (jobs > 8) jobs = 8;
        } else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
            jobs = atoi(argv[++i]);
            if (jobs < 1) jobs = 1;
            if (jobs > 8) jobs = 8;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            use_color = false;
            init_colors(&colors, use_color);
        } else if (strcmp(argv[i], "--tsan") == 0) {
            use_tsan = true;
        } else if (strcmp(argv[i], "--analyzer") == 0) {
            use_analyzer = true;
        } else if (strcmp(argv[i], "--clang-tidy") == 0) {
            use_clang_tidy = true;
        } else if (strcmp(argv[i], "--valgrind") == 0) {
            use_valgrind = true;
        } else if (strcmp(argv[i], "--max") == 0) {
            use_max = true;
        } else if (strncmp(argv[i], "--project=", 10) == 0) {
            strncpy(project_json, argv[i] + 10, sizeof(project_json) - 1);
            project_json[sizeof(project_json) - 1] = '\0';
        } else if (strncmp(argv[i], "--html=", 7) == 0) {
            html_path = argv[i] + 7;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0], &colors);
            return EXIT_SUCCESS;
        } else if (argv[i][0] != '-') {
            if (source_count < 32) {
                source_files[source_count++] = argv[i];
            }
        }
    }

    if (source_count == 0) {
        print_usage(argv[0], &colors);
        return EXIT_USAGE_ERROR;
    }

    for (i = 0; i < source_count; i++) {
        if (!file_exists(source_files[i])) {
            print_colored(&colors, colors.red, "Error: ");
            printf("file not found: %s\n", source_files[i]);
            return EXIT_FILE_NOT_FOUND;
        }

        if (!is_source_file(source_files[i])) {
            print_colored(&colors, colors.red, "Error: ");
            printf("not a C/C++ source file: %s\n", source_files[i]);
            return EXIT_USAGE_ERROR;
        }
    }

    /* Limit jobs to number of CPUs if jobs > sysconf(_SC_NPROCESSORS_ONLN) */
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count < 1) cpu_count = 1;
    if (jobs > cpu_count) {
        jobs = (int)cpu_count;
        if (jobs > 8) jobs = 8;
    }

    /* Parallel processing with fork() + waitpid() */
    /* Checking parallel mode availability */
    if (jobs > 1 && source_count > 1) {
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
                /* Child: process ONE file by execing c_tester */
                char timeout_str[32];
                char project_str[MAX_PATH_LEN + 16];
                char html_str[MAX_PATH_LEN + 16];

                snprintf(timeout_str, sizeof(timeout_str), "--timeout=%d", timeout_sec);

                /* Build argument list for child */
                int arg_idx = 0;
                char *child_argv[32];
                child_argv[arg_idx++] = "./c_tester";
                if (keep_binary) child_argv[arg_idx++] = "--keep";
                child_argv[arg_idx++] = timeout_str;
                if (use_tsan) child_argv[arg_idx++] = "--tsan";
                if (use_clang_tidy) child_argv[arg_idx++] = "--clang-tidy";
                if (use_valgrind) child_argv[arg_idx++] = "--valgrind";
                if (use_max) child_argv[arg_idx++] = "--max";
                if (project_json[0]) {
                    snprintf(project_str, sizeof(project_str), "--project=%s", project_json);
                    child_argv[arg_idx++] = project_str;
                }
                if (html_path) {
                    snprintf(html_str, sizeof(html_str), "--html=%s", html_path);
                    child_argv[arg_idx++] = html_str;
                }
                child_argv[arg_idx++] = (char *)source_files[i];
                child_argv[arg_idx++] = NULL;

                execvp("./c_tester", child_argv);
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
        return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
    }

    generate_temp_path("c_tester", binary_path, sizeof(binary_path));

    memset(&result, 0, sizeof(result));

    /* Use compile_commands.json if specified */
    if (project_json[0]) {
        char project_flags[4096];
        if (parse_compile_commands(project_json, source_files[0],
                                   project_flags, sizeof(project_flags)) == 0) {
            print_colored(&colors, colors.cyan, "[project] ");
            printf("Using flags from %s\n", project_json);
            if (run_with_compile_flags(source_files, source_count,
                                        project_flags,
                                        binary_path,
                                        result.compiler_output,
                                        sizeof(result.compiler_output)) == 0) {
                result.compilation_success = true;
            }
        } else {
            print_colored(&colors, colors.yellow, "[project] ");
            printf("Could not parse %s, falling back to auto-detection\n",
                   project_json);
        }
    }

    if (!result.compilation_success) {
        if (use_valgrind) {
            if (compile_for_valgrind(source_files, source_count, binary_path,
                                    result.compiler_output,
                                    sizeof(result.compiler_output)) == 0) {
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
        } else if (use_clang_tidy) {
            if (compile_with_clang_tidy(source_files, source_count,
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
        print_colored(&colors, colors.red, "[COMPILE ERROR]\n");
        if (result.compiler_output[0])
            printf("%s\n", result.compiler_output);
        return EXIT_COMPILE_FAIL;
    }

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    if (use_valgrind) {
        result.exit_code = run_with_valgrind(binary_path,
                                                 result.runtime_output,
                                                 sizeof(result.runtime_output),
                                                 result.sanitizer_output,
                                                 sizeof(result.sanitizer_output),
                                                 timeout_sec, NULL);
    } else {
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

    int sanitizer_count = parse_sanitizer_errors(result.sanitizer_output,
                                                   errors + error_count,
                                                   32 - error_count);
    error_count += sanitizer_count;

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

    for (i = 0; i < error_count; i++)
        generate_fix_suggestion(&errors[i]);

    print_summary(&colors, &result, error_count, errors);

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

    return error_count > 0 ? EXIT_ERRORS_FOUND : EXIT_CLEAN;
}
