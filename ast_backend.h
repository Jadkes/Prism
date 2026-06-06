/*
 * ast_backend.h - libclang AST backend for c_tester
 *
 * Purpose: Provides real AST-level code analysis by wrapping the libclang
 *          C API. Replaces the regex-based --danger scan with cursor-matched
 *          call-site detection, and provides the foundation for annotation-
 *          driven checks (Phase 2+) and symbolic execution (Phase 4).
 *
 * Design:   Opaque ASTContext holds a parsed CXTranslationUnit. Query
 *           functions (ast_find_dangerous_calls, etc.) traverse the cursor
 *           tree once at parse time and return pre-collected results so
 *           repeated queries are O(1).
 *
 * Thread-safety: Single-threaded. Each context is independent.
 */

#ifndef AST_BACKEND_H
#define AST_BACKEND_H

#include "c_tester.h"

/* ---- Data structures returned by AST queries ---- */

/* A dangerous function call found in the AST cursor tree */
typedef struct {
    char function_name[128];
    char source_file[MAX_PATH_LEN];
    unsigned line;
    unsigned column;
    int severity;
    ErrorType type;
} DangerousCall;

/* Info about a function definition */
typedef struct {
    char name[128];
    char source_file[MAX_PATH_LEN];
    unsigned line;
    unsigned column;
    unsigned param_count;
    bool returns_pointer;
} FunctionInfo;

/* A function call site (caller → callee) */
typedef struct {
    char callee[128];
    char caller[128];
    char source_file[MAX_PATH_LEN];
    unsigned line;
    unsigned column;
} CallSite;

/* Opaque context — allocated by ast_parse, freed by ast_free */
typedef struct ASTContext ASTContext;

/* ---- AST Backend API ---- */

/*
 * ast_parse - Parse a C source file into an AST context
 *
 * WHY: Libclang's cursor tree enables precise call-site detection
 *      without false positives from string literals or comments.
 *
 * @param source_file - Path to C source file to parse
 * @param compiler_args - NULL-terminated array of extra compiler flags
 *                        (e.g. {"-Iinclude", "-DDEBUG", NULL}).
 *                        Pass NULL for defaults.
 * @return Opaque context, or NULL on failure. Must be ast_free()'d.
 */
ASTContext *ast_parse(const char *source_file, const char **compiler_args);

/*
 * ast_free - Free all resources held by an AST context
 */
void ast_free(ASTContext *ctx);

/*
 * ast_find_dangerous_calls - Get all dangerous API calls found during parse
 *
 * Returns pre-collected results from the single AST traversal.
 * The calls are filtered to user source files only (no system headers).
 *
 * @param out - Array to fill with DangerousCall records
 * @param max - Capacity of out array
 * @return Number of dangerous calls found
 */
int ast_get_dangerous_calls(ASTContext *ctx,
                            DangerousCall *out, int max);

/*
 * ast_get_function_count - Number of function definitions found
 */
int ast_get_function_count(ASTContext *ctx);

/*
 * ast_get_functions - Get function definition info
 *
 * @param out - Array to fill with FunctionInfo records
 * @param max - Capacity of out array
 * @return Number of functions written to out
 */
int ast_get_functions(ASTContext *ctx, FunctionInfo *out, int max);

/*
 * ast_find_calls_by_name - Find all call sites of a named function
 *
 * Scans the pre-collected call list for a specific callee name.
 * Used by Phase 3+ for malloc/free pair tracking, lock/unlock, etc.
 *
 * @param func_name - Exact callee name to search for
 * @param out - Array to fill with CallSite records
 * @param max - Capacity of out array
 * @return Number of call sites found
 */
int ast_find_calls_by_name(ASTContext *ctx, const char *func_name,
                           CallSite *out, int max);

#endif /* AST_BACKEND_H */
