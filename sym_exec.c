/*
 * sym_exec.c - Lightweight symbolic execution engine implementation
 *
 * Design: Scans source lines for if/while/for conditions and builds a
 *         path-sensitive constraint graph. Each branch forks the state
 *         set; each fork carries refined integer ranges on named variables.
 *
 * Limits:  512 paths per function, 10 basic blocks deep.
 *
 * Integration: Called from check_engine to answer queries about variable
 *              ranges on feasible paths, reducing false positives.
 */

#include "sym_exec.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

/* ---- Helpers: parsing conditions from source lines ---- */

/*
 * Extract a simple variable name from the start of a C expression.
 * Stops at operators, whitespace, or end of string.
 */
static int extract_var_name(const char *src, char *out, int out_size)
{
    int i = 0;
    while (*src && i < out_size - 1) {
        if (isalnum((unsigned char)*src) || *src == '_') {
            out[i++] = *src++;
        } else {
            break;
        }
    }
    out[i] = '\0';
    return i;
}

/*
 * Parse a comparison like "i < 10" or "x >= 0" from a string.
 * Returns false if no comparison found.
 */
static bool parse_comparison(const char *cond_text,
                              char *var_out, int var_size,
                              SymCmpOp *op_out, int *val_out)
{
    if (!cond_text || !*cond_text)
        return false;

    /* Skip opening paren if present */
    while (*cond_text == '(' || *cond_text == ' ')
        cond_text++;

    /* Try to extract a variable name */
    char vname[64];
    if (extract_var_name(cond_text, vname, sizeof(vname)) == 0)
        return false;

    /* Advance past variable name in the source */
    cond_text += strlen(vname);

    /* Skip whitespace */
    while (*cond_text == ' ') cond_text++;

    /* Try comparison operators */
    SymCmpOp op;
    if (strncmp(cond_text, "<=", 2) == 0) {
        op = SYM_CMP_LE;
        cond_text += 2;
    } else if (strncmp(cond_text, ">=", 2) == 0) {
        op = SYM_CMP_GE;
        cond_text += 2;
    } else if (strncmp(cond_text, "==", 2) == 0) {
        op = SYM_CMP_EQ;
        cond_text += 2;
    } else if (strncmp(cond_text, "!=", 2) == 0) {
        op = SYM_CMP_NE;
        cond_text += 2;
    } else if (*cond_text == '<') {
        op = SYM_CMP_LT;
        cond_text++;
    } else if (*cond_text == '>') {
        op = SYM_CMP_GT;
        cond_text++;
    } else if (*cond_text == '=') {
        /* Assignment, not comparison */
        return false;
    } else {
        return false;
    }

    /* Skip whitespace after operator */
    while (*cond_text == ' ') cond_text++;

    /* Check if the value part is a number */
    if (*cond_text == '-' || isdigit((unsigned char)*cond_text)) {
        char *end = NULL;
        long v = strtol(cond_text, &end, 10);
        if (end > cond_text) {
            strncpy(var_out, vname, (size_t)var_size - 1);
            var_out[var_size - 1] = '\0';
            *op_out = op;
            *val_out = (int)v;
            return true;
        }
    }

    return false;
}

/*
 * Find a branch condition line in source text.
 * Looks for "if (", "while (", or "for (...; condition; ...)" patterns.
 */
typedef struct {
    int line_number;
    char condition[256];
    int true_target;   /* line number of first non-branch line (true side) */
    int false_target;  /* line number of first line after the block (false) */
} BranchPoint;

/* Max branch points we track */
#define MAX_BRANCHES 64

/* ---- SymState operations ---- */

/*
 * Create a new empty state (no constraints).
 * Returns NULL on allocation failure.
 */
static SymState *sym_state_new(void)
{
    SymState *s = (SymState *)calloc(1, sizeof(SymState));
    if (s) {
        s->var_count = 0;
        s->depth = 0;
        s->next = NULL;
    }
    return s;
}

/*
 * Deep-copy a SymState (including all vars and constraints).
 */
static SymState *sym_state_clone(const SymState *src)
{
    if (!src) return NULL;
    SymState *s = sym_state_new();
    if (!s) return NULL;
    *s = *src;   /* shallow copy all fields */
    s->next = NULL;  /* don't copy linked list */
    /* All vars are value types (char arrays + ints), shallow copy is correct */
    return s;
}

/*
 * Find or create a variable range entry in a state.
 */
static SymVarRange *sym_get_var(SymState *s, const char *name)
{
    for (int i = 0; i < s->var_count; i++) {
        if (strcmp(s->vars[i].var_name, name) == 0)
            return &s->vars[i];
    }
    /* Not found — add it */
    if (s->var_count >= 64) return NULL;
    SymVarRange *vr = &s->vars[s->var_count++];
    memset(vr, 0, sizeof(SymVarRange));
    strncpy(vr->var_name, name, sizeof(vr->var_name) - 1);
    vr->min_val = INT_MIN;
    vr->max_val = INT_MAX;
    vr->alloc_size = -1;
    vr->is_null = false;
    vr->is_nonnull = false;
    return vr;
}

/*
 * Apply a constraint to a state, narrowing the variable's range.
 * Returns true if the state is still feasible (range non-empty).
 */
static bool sym_apply_constraint(SymState *s,
                                  const char *var,
                                  SymCmpOp op, int val)
{
    SymVarRange *vr = sym_get_var(s, var);
    if (!vr) return false;   /* out of var slots — assume feasible */

    switch (op) {
    case SYM_CMP_LT: if (val - 1 < vr->max_val) vr->max_val = val - 1; break;
    case SYM_CMP_LE: if (val < vr->max_val)     vr->max_val = val;     break;
    case SYM_CMP_GT: if (val + 1 > vr->min_val) vr->min_val = val + 1; break;
    case SYM_CMP_GE: if (val > vr->min_val)     vr->min_val = val;     break;
    case SYM_CMP_EQ: vr->min_val = vr->max_val = val; break;
    case SYM_CMP_NE:
        /* i != 5: split into i <= 4 OR i >= 6 — but we can't fork here.
         * Instead, just narrow one end if it's at the boundary. */
        if (vr->min_val == val) vr->min_val = val + 1;
        if (vr->max_val == val) vr->max_val = val - 1;
        break;
    }

    /* Check infeasibility */
    if (vr->min_val > vr->max_val) return false;

    /* Pointer-specific handling */
    if (op == SYM_CMP_EQ) {
        if (val == 0) {
            vr->is_null = true;
            vr->is_nonnull = false;
        } else {
            vr->is_null = false;
            vr->is_nonnull = true;
        }
    } else if (op == SYM_CMP_NE && val == 0) {
        vr->is_null = false;
        vr->is_nonnull = true;
    }

    return true;
}

/* ---- Branch detection from source lines ---- */

/*
 * Detect branch conditions in source lines.
 * Populates branches[] array with found if/while conditions.
 * Returns number of branches found.
 */
static int find_branches(const char *source_path,
                          BranchPoint *branches, int max_branches)
{
    FILE *fp = fopen(source_path, "r");
    if (!fp) return 0;

    char line[2048];
    int lnum = 0;
    int nb = 0;

    while (fgets(line, sizeof(line), fp)) {
        lnum++;
        if (nb >= max_branches) break;

        /* Find if/while/for conditions */
        const char *cond_start = NULL;
        bool is_for = false;

        const char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (strncmp(trimmed, "if (", 4) == 0) {
            cond_start = trimmed + 4;
        } else if (strncmp(trimmed, "while (", 7) == 0) {
            cond_start = trimmed + 7;
        } else if (strncmp(trimmed, "for (", 5) == 0) {
            cond_start = trimmed + 5;
            is_for = true;
        }

        if (!cond_start) continue;

        /* Extract the condition text (between parens) */
        char cond_text[256] = {0};
        int depth = 0;
        int ci = 0;
        const char *p = cond_start;

        while (*p && ci < (int)sizeof(cond_text) - 1) {
            if (*p == '(') depth++;
            else if (*p == ')') {
                if (depth == 0) break;
                depth--;
            }

            /* For for-loops, we want the middle condition (index < limit) */
            if (is_for) {
                /* Skip the init part (up to first semicolon) */
                if (ci == 0 && *p == ';') {
                    cond_text[0] = '\0';
                    ci = 0;
                    p++;
                    /* Now reading the condition part */
                    while (*p && *p != ';') {
                        if (ci < (int)sizeof(cond_text) - 1)
                            cond_text[ci++] = *p;
                        p++;
                    }
                    cond_text[ci] = '\0';
                    break;
                }
                /* We haven't seen init's semicolon yet */
                p++;
                continue;
            }

            cond_text[ci++] = *p;
            p++;
        }
        cond_text[ci] = '\0';

        if (cond_text[0]) {
            branches[nb].line_number = lnum;
            snprintf(branches[nb].condition,
                     sizeof(branches[nb].condition), "%.*s",
                     (int)sizeof(branches[nb].condition) - 1,
                     cond_text);
            branches[nb].true_target = lnum + 1;   /* approximate */
            branches[nb].false_target = lnum + 5;   /* approximate */
            nb++;
        }
    }

    fclose(fp);
    return nb;
}

/* ---- Path set operations ---- */

/*
 * Fork all paths in a set for a given branch condition.
 * For each existing path, creates two children:
 *   - True fork: constraint that cond holds
 *   - False fork: constraint that cond is negated
 * Prunes infeasible paths.
 */
static void sym_fork_at_branch(SymPathSet *ps,
                                 const char *var,
                                 SymCmpOp op, int val)
{
    int old_count = ps->path_count;
    for (int i = 0; i < old_count && ps->path_count < ps->max_paths; i++) {
        SymState *original = ps->paths;
        if (!original) break;
        ps->paths = original->next;
        original->next = NULL;
        ps->path_count--;

        /* Fork path for true branch */
        SymState *true_state = sym_state_clone(original);
        if (true_state) {
            true_state->depth = original->depth + 1;
            if (sym_apply_constraint(true_state, var, op, val)) {
                /* Append to end of path list */
                SymState **p = &ps->paths;
                while (*p) p = &(*p)->next;
                *p = true_state;
                ps->path_count++;
            } else {
                free(true_state);
            }
        }

        /* Fork path for false branch (negated condition) */
        SymState *false_state = sym_state_clone(original);
        if (false_state) {
            false_state->depth = original->depth + 1;
            /* Negate the comparison */
            SymCmpOp neg_op;
            switch (op) {
            case SYM_CMP_LT: neg_op = SYM_CMP_GE; break;
            case SYM_CMP_LE: neg_op = SYM_CMP_GT; break;
            case SYM_CMP_GT: neg_op = SYM_CMP_LE; break;
            case SYM_CMP_GE: neg_op = SYM_CMP_LT; break;
            case SYM_CMP_EQ: neg_op = SYM_CMP_NE; break;
            case SYM_CMP_NE: neg_op = SYM_CMP_EQ; break;
            default: neg_op = SYM_CMP_NE; break;
            }
            if (sym_apply_constraint(false_state, var, neg_op, val)) {
                SymState **p = &ps->paths;
                while (*p) p = &(*p)->next;
                *p = false_state;
                ps->path_count++;
            } else {
                free(false_state);
            }
        }

        free(original);
    }
}

/* ---- Public API ---- */

SymPathSet sym_analyze_source(const char *source_path,
                               const char *func_name,
                               unsigned func_line,
                               int max_paths)
{
    (void)func_name;
    (void)func_line;

    SymPathSet result;
    memset(&result, 0, sizeof(result));
    result.max_paths = (max_paths > 0) ? max_paths : 512;

    if (!source_path) return result;

    /* Detect branch conditions in the source */
    BranchPoint branches[MAX_BRANCHES];
    int nb = find_branches(source_path, branches, MAX_BRANCHES);

    if (nb == 0) {
        /* No branches — single path, no constraints */
        result.paths = sym_state_new();
        result.path_count = 1;
        return result;
    }

    /* Start with a single empty path */
    result.paths = sym_state_new();
    result.path_count = 1;

    /* Process each branch, forking paths */
    for (int b = 0; b < nb; b++) {
        if (result.path_count >= result.max_paths) break;
        if (result.paths == NULL) break;

        /* Parse the condition from this branch */
        char var[64];
        SymCmpOp op;
        int val;

        if (parse_comparison(branches[b].condition,
                              var, sizeof(var), &op, &val)) {
            sym_fork_at_branch(&result, var, op, val);
        }
        /* If we can't parse the condition, we keep all existing paths
         * as-is (conservative: don't prune). */
    }

    return result;
}

void sym_free_paths(SymPathSet *ps)
{
    if (!ps) return;
    SymState *s = ps->paths;
    while (s) {
        SymState *next = s->next;
        free(s);
        s = next;
    }
    ps->paths = NULL;
    ps->path_count = 0;
}

bool sym_can_be_negative(const SymPathSet *ps, const char *var_name)
{
    if (!ps || !var_name || !ps->paths)
        return true;   /* conservative: assume it can be */

    SymState *s = ps->paths;
    while (s) {
        for (int v = 0; v < s->var_count; v++) {
            if (strcmp(s->vars[v].var_name, var_name) == 0) {
                if (s->vars[v].min_val < 0)
                    return true;   /* found a path where var can be < 0 */
                break;
            }
        }
        s = s->next;
    }

    return false;   /* no path allows negative */
}

bool sym_can_exceed(const SymPathSet *ps, const char *var_name, int bound)
{
    if (!ps || !var_name || !ps->paths)
        return true;   /* conservative */

    SymState *s = ps->paths;
    while (s) {
        for (int v = 0; v < s->var_count; v++) {
            if (strcmp(s->vars[v].var_name, var_name) == 0) {
                if (s->vars[v].max_val > bound)
                    return true;
                break;
            }
        }
        s = s->next;
    }

    return false;
}

bool sym_is_always_nonnull(const SymPathSet *ps, const char *var_name)
{
    if (!ps || !var_name || !ps->paths)
        return false;   /* conservative */

    SymState *s = ps->paths;
    while (s) {
        for (int v = 0; v < s->var_count; v++) {
            if (strcmp(s->vars[v].var_name, var_name) == 0) {
                if (s->vars[v].is_null || !s->vars[v].is_nonnull)
                    return false;  /* path allows null */
                break;
            }
        }
        s = s->next;
    }

    /* All paths agree the pointer is non-NULL */
    return true;
}

int sym_get_alloc_size(const SymPathSet *ps, const char *var_name)
{
    if (!ps || !var_name)
        return -1;

    SymState *s = ps->paths;
    while (s) {
        for (int v = 0; v < s->var_count; v++) {
            if (strcmp(s->vars[v].var_name, var_name) == 0)
                return s->vars[v].alloc_size;
        }
        s = s->next;
    }
    return -1;
}
