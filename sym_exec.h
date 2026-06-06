/*
 * sym_exec.h - Lightweight symbolic execution engine for c_tester
 *
 * Purpose: Provides path-sensitive analysis by tracking variable state
 *          across branches. Used by check modules to reduce false positives
 *          by eliminating infeasible paths and invalidating impossible
 *          buffer accesses.
 *
 * Design: Intraprocedural only. Walks each function's AST cursor tree,
 *         forking state at if/while/for branch points. Each path carries
 *         a set of integer range constraints on named variables. Infeasible
 *         paths (contradictory constraints) are pruned.
 *
 * Limits:  Max 512 paths per function, max 10 basic blocks deep.
 *
 * Thread-safety: Single-threaded. Each run is independent.
 */

#ifndef SYM_EXEC_H
#define SYM_EXEC_H

#include "c_tester.h"
#include "ast_backend.h"

/* ---- Constraint and state types ---- */

typedef enum {
    SYM_CMP_LT,   /* var < val  */
    SYM_CMP_LE,   /* var <= val */
    SYM_CMP_GT,   /* var > val  */
    SYM_CMP_GE,   /* var >= val */
    SYM_CMP_EQ,   /* var == val */
    SYM_CMP_NE,   /* var != val */
} SymCmpOp;

/* A single constraint on a variable */
typedef struct {
    char var_name[64];
    SymCmpOp op;
    int value;
} SymConstraint;

/* Range state for a variable */
typedef struct {
    char var_name[64];
    int min_val;        /* inclusive lower bound */
    int max_val;        /* inclusive upper bound */
    int alloc_size;     /* if the variable is a pointer, how big is allocation? */
    bool is_null;       /* pointer is known to be NULL */
    bool is_nonnull;    /* pointer is known to be non-NULL */
} SymVarRange;

/* A single symbolic state (one feasible path) */
typedef struct SymState {
    SymVarRange vars[64];
    int var_count;
    int depth;          /* basic blocks from function entry */
    struct SymState *next;  /* linked list in SymPathSet */
} SymState;

/* Collection of all feasible paths for a function */
typedef struct {
    SymState *paths;    /* linked list of states (one per live path) */
    int path_count;     /* current number of live paths */
    int max_paths;      /* cap (default 512) */
} SymPathSet;

/*
 * sym_analyze_source - Run symbolic execution on a source file
 *
 * Parses the source line-by-line for branch conditions and builds
 * a path-sensitive constraint graph. Returns a set of feasible paths.
 *
 * @param source_path - Path to C source file
 * @param func_name   - Function to analyze (NULL = all functions)
 * @param func_line   - Line number of function definition
 * @param max_paths   - Maximum number of paths to explore
 * @return SymPathSet with feasible paths (must be freed with sym_free_paths)
 */
SymPathSet sym_analyze_source(const char *source_path,
                               const char *func_name,
                               unsigned func_line,
                               int max_paths);

/*
 * sym_free_paths - Free a path set returned by sym_analyze_source
 */
void sym_free_paths(SymPathSet *ps);

/*
 * sym_can_be_negative - Check if a variable can be < 0 on any feasible path
 *
 * Returns true if at least one path allows the variable to be negative.
 * Used by check modules to decide whether to flag out-of-bounds accesses.
 */
bool sym_can_be_negative(const SymPathSet *ps, const char *var_name);

/*
 * sym_can_exceed - Check if a variable can exceed a bound on any path
 *
 * Returns true if any feasible path allows var > bound.
 * Used to check array indices against allocation sizes.
 */
bool sym_can_exceed(const SymPathSet *ps, const char *var_name, int bound);

/*
 * sym_is_always_nonnull - Check if a pointer is non-NULL on all paths
 *
 * Returns true only if var is guaranteed non-NULL on every feasible path.
 */
bool sym_is_always_nonnull(const SymPathSet *ps, const char *var_name);

/*
 * sym_get_alloc_size - Get allocation size for a pointer variable
 *
 * Returns the tracked allocation size (from malloc bytes), or -1 if unknown.
 */
int sym_get_alloc_size(const SymPathSet *ps, const char *var_name);

#endif /* SYM_EXEC_H */
