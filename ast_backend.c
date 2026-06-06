/*
 * ast_backend.c - libclang AST backend implementation
 *
 * Purpose: Wraps libclang's C API to parse C source files and traverse
 *          the AST cursor tree. Collects dangerous function calls,
 *          function definitions, and all call sites in a single pass.
 *
 * Design: One-pass AST traversal during ast_parse() collects everything.
 *         Query functions return from pre-populated arrays.
 */

#include "ast_backend.h"
#include <clang-c/Index.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Internal data structures ---- */

struct ASTContext {
    CXIndex index;
    CXTranslationUnit tu;
    char user_source_file[MAX_PATH_LEN];
    char current_function[128];

    /* Collected dangerous calls (from single-pass traversal) */
    DangerousCall *dangerous_calls;
    int dangerous_cap;
    int dangerous_count;

    /* Collected function definitions */
    FunctionInfo *functions;
    int func_cap;
    int func_count;

    /* All call sites (for later lookup by ast_find_calls_by_name) */
    CallSite *all_calls;
    int call_cap;
    int call_count;
};

/* ---- Dangerous function pattern table ---- */

typedef struct {
    const char *name;
    const char *title;
    const char *fix;
    ErrorType type;
    int severity;
} DangerPattern;

static const DangerPattern dangerous_funcs[] = {
    {"gets", "Unsafe gets() Call",
     "Use fgets(buf, sizeof(buf), stdin). gets() has no bounds checking.",
     ERR_BUFFER_OVERFLOW, 3},
    {"strcpy", "Unsafe strcpy() Use",
     "Use strncpy() with explicit length check, or strlcpy() if available.",
     ERR_BUFFER_OVERFLOW, 3},
    {"sprintf", "Unsafe sprintf() Use",
     "Use snprintf(buf, sizeof(buf), ...). sprintf() does not check size.",
     ERR_BUFFER_OVERFLOW, 3},
    {"strcat", "Unsafe strcat() Use",
     "Check buffer space before concatenation, or use strlcat().",
     ERR_BUFFER_OVERFLOW, 3},
    {"scanf", "Unbounded scanf()",
     "Always specify field width: scanf(\"%%%ds\", ...).",
     ERR_BUFFER_OVERFLOW, 2},
    {"alloca", "Stack Allocation (alloca)",
     "Use malloc() instead. alloca() can silently cause stack overflow.",
     ERR_STACK_OVERFLOW, 2},
    {"setjmp", "setjmp Without Volatile",
     "Variables modified between setjmp/longjmp must be volatile.",
     ERR_UNINIT_VAR, 1},
    {"strtok", "Unsafe strtok() Use",
     "strtok() is not reentrant. Use strtok_r() instead.",
     ERR_UNKNOWN, 2},
    {"itoa", "Unsafe itoa() Use",
     "itoa() is not standard C. Use snprintf() with %%d/%%u format.",
     ERR_UNKNOWN, 2},
    {"asctime", "Unsafe asctime() Use",
     "asctime() is not thread-safe. Use strftime() instead.",
     ERR_UNKNOWN, 1},
    {"tmpnam", "Unsafe tmpnam() Use",
     "tmpnam() has race conditions. Use mkstemp() or tmpfile().",
     ERR_UNKNOWN, 2},
    {"getenv", "Unchecked getenv() Use",
     "getenv() can return NULL. Always check the return value.",
     ERR_NULL_DEREF, 2},
};

static const int num_dangerous = sizeof(dangerous_funcs)
                                 / sizeof(dangerous_funcs[0]);

/* ---- Helpers ---- */

static const DangerPattern *match_dangerous(const char *name)
{
    for (int i = 0; i < num_dangerous; i++) {
        if (strcmp(name, dangerous_funcs[i].name) == 0)
            return &dangerous_funcs[i];
    }
    return NULL;
}

/* Get source location info from a cursor. Returns false if unavailable. */
static bool get_cursor_location(CXCursor cursor,
                                 char *file_buf, size_t file_size,
                                 unsigned *line, unsigned *column)
{
    CXSourceLocation loc = clang_getCursorLocation(cursor);
    if (clang_Location_isFromMainFile(loc) == 0)
        return false;

    CXString filename;
    clang_getPresumedLocation(loc, &filename, line, column);

    const char *fstr = clang_getCString(filename);
    if (!fstr) {
        clang_disposeString(filename);
        return false;
    }
    strncpy(file_buf, fstr, file_size - 1);
    file_buf[file_size - 1] = '\0';
    clang_disposeString(filename);
    return true;
}

/* Grow a dynamically-allocated array. Returns 0 on failure. */
#define GROW_ARRAY(ptr, cap, count, elem_size) \
    (((count) < (cap)) ? 0 : grow_array_impl((void**)&(ptr), &(cap), \
                                              (elem_size)))

static int grow_array_impl(void **ptr, int *cap, size_t elem_size)
{
    int new_cap = (*cap == 0) ? 64 : (*cap) * 2;
    void *tmp = realloc(*ptr, (size_t)new_cap * elem_size);
    if (!tmp) return -1;
    *ptr = tmp;
    *cap = new_cap;
    return 0;
}

/* ---- AST cursor visitor ---- */

static enum CXChildVisitResult visit_cursor(CXCursor cursor,
                                             CXCursor parent,
                                             CXClientData data)
{
    (void)parent;
    ASTContext *ctx = (ASTContext *)data;
    int kind = (int)clang_getCursorKind(cursor);

    /* Track function definitions */
    if (kind == (int)CXCursor_FunctionDecl) {
        CXString name = clang_getCursorSpelling(cursor);
        const char *nstr = clang_getCString(name);

        CXType func_type = clang_getCursorType(cursor);
        int num_params = clang_getNumArgTypes(func_type);
        CXType ret_type = clang_getResultType(func_type);
        int ret_ptr = (ret_type.kind == CXType_Pointer);

        char file[MAX_PATH_LEN] = {0};
        unsigned line = 0, col = 0;
        if (nstr && get_cursor_location(cursor, file, sizeof(file),
                                         &line, &col)) {
            if (grow_array_impl((void**)&ctx->functions, &ctx->func_cap,
                                sizeof(FunctionInfo)) == 0) {
                int idx = ctx->func_count++;
                strncpy(ctx->functions[idx].name, nstr,
                        sizeof(ctx->functions[idx].name) - 1);
                strncpy(ctx->functions[idx].source_file, file,
                        sizeof(ctx->functions[idx].source_file) - 1);
                ctx->functions[idx].line = line;
                ctx->functions[idx].column = col;
                ctx->functions[idx].param_count = (unsigned)num_params;
                ctx->functions[idx].returns_pointer = (ret_ptr != 0);
            }
        }

        /* Update current function for call site tracking */
        if (nstr)
            strncpy(ctx->current_function, nstr,
                    sizeof(ctx->current_function) - 1);

        clang_disposeString(name);
        return CXChildVisit_Recurse;
    }

    /* Track function calls */
    if (kind == (int)CXCursor_CallExpr) {
        CXCursor callee = clang_getCursorReferenced(cursor);
        CXString cname = clang_getCursorSpelling(callee);
        const char *cstr = clang_getCString(cname);

        if (cstr && cstr[0]) {
            char file[MAX_PATH_LEN] = {0};
            unsigned line = 0, col = 0;
            if (get_cursor_location(cursor, file, sizeof(file),
                                     &line, &col)) {
                /* Record in all_calls for later lookup */
                if (grow_array_impl((void**)&ctx->all_calls,
                                    &ctx->call_cap,
                                    sizeof(CallSite)) == 0) {
                    int cidx = ctx->call_count++;
                    strncpy(ctx->all_calls[cidx].callee, cstr,
                            sizeof(ctx->all_calls[cidx].callee) - 1);
                    strncpy(ctx->all_calls[cidx].caller, ctx->current_function,
                            sizeof(ctx->all_calls[cidx].caller) - 1);
                    strncpy(ctx->all_calls[cidx].source_file, file,
                            sizeof(ctx->all_calls[cidx].source_file) - 1);
                    ctx->all_calls[cidx].line = line;
                    ctx->all_calls[cidx].column = col;
                }

                /* Check if this is a dangerous function */
                const DangerPattern *dp = match_dangerous(cstr);
                if (dp) {
                    if (grow_array_impl((void**)&ctx->dangerous_calls,
                                        &ctx->dangerous_cap,
                                        sizeof(DangerousCall)) == 0) {
                        int didx = ctx->dangerous_count++;
                        strncpy(ctx->dangerous_calls[didx].function_name,
                                cstr,
                                sizeof(ctx->dangerous_calls[didx]
                                       .function_name) - 1);
                        strncpy(ctx->dangerous_calls[didx].source_file,
                                file,
                                sizeof(ctx->dangerous_calls[didx]
                                       .source_file) - 1);
                        ctx->dangerous_calls[didx].line = line;
                        ctx->dangerous_calls[didx].column = col;
                        ctx->dangerous_calls[didx].severity = dp->severity;
                        ctx->dangerous_calls[didx].type = dp->type;
                    }
                }
            }
        }
        clang_disposeString(cname);
        return CXChildVisit_Recurse;
    }

    /* Recurse into compound statements, functions, etc. */
    return CXChildVisit_Recurse;
}

/* ---- Public API ---- */

ASTContext *ast_parse(const char *source_file, const char **compiler_args)
{
    if (!source_file)
        return NULL;

    ASTContext *ctx = calloc(1, sizeof(ASTContext));
    if (!ctx) return NULL;

    strncpy(ctx->user_source_file, source_file,
            sizeof(ctx->user_source_file) - 1);
    ctx->current_function[0] = '\0';

    /* Create index (1 = exclude PCH from diagnostics) */
    ctx->index = clang_createIndex(1, 0);

    /* Build clang argv */
    enum { MAX_CLANG_ARGS = 64 };
    const char *argv[MAX_CLANG_ARGS];
    int argc = 0;

    /* Determine language from extension */
    const char *ext = strrchr(source_file, '.');
    if (ext && (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cxx") == 0 ||
                strcmp(ext, ".cc") == 0))
        argv[argc++] = "-xc++";
    else
        argv[argc++] = "-xc";

    argv[argc++] = "-std=gnu11";
    argv[argc++] = "-w";   /* suppress clang's own warnings (we want AST) */
    argv[argc++] = "-I.";

    /* Append user compiler_args */
    if (compiler_args) {
        for (int i = 0; compiler_args[i] && argc < MAX_CLANG_ARGS - 1; i++)
            argv[argc++] = compiler_args[i];
    }

    argv[argc] = NULL;

    /* Parse translation unit */
    unsigned tu_flags = CXTranslationUnit_KeepGoing |
                        CXTranslationUnit_VisitImplicitAttributes;

    ctx->tu = clang_parseTranslationUnit(ctx->index, source_file,
                                         argv, argc,
                                         NULL, 0, tu_flags);

    if (!ctx->tu) {
        clang_disposeIndex(ctx->index);
        free(ctx);
        return NULL;
    }

    /* Single-pass traversal to collect everything */
    CXCursor cursor = clang_getTranslationUnitCursor(ctx->tu);
    clang_visitChildren(cursor, visit_cursor, ctx);

    return ctx;
}

void ast_free(ASTContext *ctx)
{
    if (!ctx) return;
    if (ctx->tu)
        clang_disposeTranslationUnit(ctx->tu);
    if (ctx->index)
        clang_disposeIndex(ctx->index);
    free(ctx->dangerous_calls);
    free(ctx->functions);
    free(ctx->all_calls);
    free(ctx);
}

int ast_get_dangerous_calls(ASTContext *ctx,
                            DangerousCall *out, int max)
{
    if (!ctx || !out || max <= 0)
        return 0;

    int written = 0;
    for (int i = 0; i < ctx->dangerous_count && written < max; i++) {
        /* Only return findings from user's source file */
        if (strcmp(ctx->dangerous_calls[i].source_file,
                   ctx->user_source_file) != 0)
            continue;
        out[written++] = ctx->dangerous_calls[i];
    }
    return written;
}

int ast_get_function_count(ASTContext *ctx)
{
    return ctx ? ctx->func_count : 0;
}

int ast_get_functions(ASTContext *ctx, FunctionInfo *out, int max)
{
    if (!ctx || !out || max <= 0)
        return 0;

    int written = 0;
    for (int i = 0; i < ctx->func_count && written < max; i++) {
        if (strcmp(ctx->functions[i].source_file,
                   ctx->user_source_file) != 0)
            continue;
        out[written++] = ctx->functions[i];
    }
    return written;
}

int ast_find_calls_by_name(ASTContext *ctx, const char *func_name,
                           CallSite *out, int max)
{
    if (!ctx || !func_name || !out || max <= 0)
        return 0;

    int written = 0;
    for (int i = 0; i < ctx->call_count && written < max; i++) {
        if (strcmp(ctx->all_calls[i].callee, func_name) == 0)
            out[written++] = ctx->all_calls[i];
    }
    return written;
}
