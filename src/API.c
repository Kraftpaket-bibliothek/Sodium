#include "API.h"
#include "Common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/time.h>
#endif

// ----------------------------------------------------------------------------
// High-Resolution Timer Helper
// ----------------------------------------------------------------------------

static double get_time_ms(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return ((double)count.QuadPart * 1000.0) / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
#endif
}

// ----------------------------------------------------------------------------
// Dynamic String Builder for JSON
// ----------------------------------------------------------------------------

typedef struct {
    char*  data;
    size_t length;
    size_t capacity;
} ApiStringBuilder;

static void api_sb_init(ApiStringBuilder* sb, size_t initial_cap) {
    sb->capacity = initial_cap < 64 ? 64 : initial_cap;
    sb->data = (char*)malloc(sb->capacity);
    sb->data[0] = '\0';
    sb->length = 0;
}

static void api_sb_ensure(ApiStringBuilder* sb, size_t extra) {
    if (sb->length + extra + 1 >= sb->capacity) {
        sb->capacity = (sb->capacity * 2) + extra + 128;
        sb->data = (char*)realloc(sb->data, sb->capacity);
    }
}

static void api_sb_append_str(ApiStringBuilder* sb, const char* str) {
    if (!str) return;
    size_t len = strlen(str);
    api_sb_ensure(sb, len);
    memcpy(sb->data + sb->length, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}


static void api_sb_append_int(ApiStringBuilder* sb, long long val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", val);
    api_sb_append_str(sb, buf);
}

static void api_sb_append_double(ApiStringBuilder* sb, double val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", val);
    api_sb_append_str(sb, buf);
}

// ----------------------------------------------------------------------------
// Internal DIMACS Ingestion Helper
// ----------------------------------------------------------------------------

static bool parse_dimacs_into_solver(const char* dimacs_str, SodiumSolver* solver, int* out_max_var) {
    if (!dimacs_str || !solver) return false;

    const char* p = dimacs_str;
    int max_var = 0;
    int current_clause_len = 0;

    while (*p) {
        // Skip leading whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        // Skip comments
        if (*p == 'c') {
            while (*p && *p != '\n') p++;
            continue;
        }

        // Problem line: p cnf <nvars> <nclauses>
        if (*p == 'p') {
            while (*p && *p != '\n') {
                if (strncmp(p, "cnf", 3) == 0) {
                    p += 3;
                    char* end = NULL;
                    long vars = strtol(p, &end, 10);
                    if (end != p && vars > 0) {
                        max_var = (int)vars;
                    }
                    p = end ? end : p;
                } else {
                    p++;
                }
            }
            continue;
        }

        // Literal number
        char* end = NULL;
        long lit = strtol(p, &end, 10);
        if (end == p) {
            p++; // unrecognized char, advance
            continue;
        }

        p = end;
        int lit_val = (int)lit;
        sodium_solver_add_literal(solver, lit_val);

        if (lit_val == 0) {
            current_clause_len = 0;
        } else {
            current_clause_len++;
            int v = abs(lit_val);
            if (v > max_var) max_var = v;
        }
    }

    // Auto-close trailing unclosed clause
    if (current_clause_len > 0) {
        sodium_solver_add_literal(solver, 0);
    }

    if (out_max_var) *out_max_var = max_var;
    return true;
}

// ============================================================================
// SECTION 1: DIMACS SOLVER & JSON RESPONSE BUILDER
// ============================================================================

int sodium_api_solve_dimacs(const char* dimacs_str, char** out_json_response) {
    if (!out_json_response) return SODIUM_STATUS_UNKNOWN;
    *out_json_response = NULL;

    if (!dimacs_str || strlen(dimacs_str) == 0) {
        LOG_WARN("API", "sodium_api_solve_dimacs received empty/null DIMACS input");
        ApiStringBuilder err_sb;
        api_sb_init(&err_sb, 128);
        api_sb_append_str(&err_sb,
            "{\n  \"format\": \"cnf\",\n  \"response\": {\n"
            "    \"status\": \"UNKNOWN\",\n"
            "    \"status_code\": 0,\n"
            "    \"error\": \"Empty or null DIMACS input\"\n"
            "  }\n}");
        *out_json_response = err_sb.data;
        return SODIUM_STATUS_UNKNOWN;
    }

    LOG_INFO("API", "sodium_api_solve_dimacs: Ingesting DIMACS formula (%zu bytes)", strlen(dimacs_str));
    SodiumSolver* solver = sodium_solver_create();
    int max_var = 0;

    double t_start = get_time_ms();
    parse_dimacs_into_solver(dimacs_str, solver, &max_var);

    int status_code = sodium_solver_solve(solver);
    double t_elapsed_ms = get_time_ms() - t_start;

    const char* status_str = (status_code == SODIUM_STATUS_SATISFIABLE) ? "SATISFIABLE" :
                            ((status_code == SODIUM_STATUS_UNSATISFIABLE) ? "UNSATISFIABLE" : "UNKNOWN");

    int final_var_count = sodium_solver_get_variable_count(solver);
    if (final_var_count > max_var) max_var = final_var_count;
    int clause_count = sodium_solver_get_clause_count(solver);

    LOG_INFO("SOLVER", "DIMACS solved: %s (Status: %d, Vars: %d, Clauses: %d, Time: %.4f ms)",
             status_str, status_code, max_var, clause_count, t_elapsed_ms);

    ApiStringBuilder sb;
    api_sb_init(&sb, 1024);

    api_sb_append_str(&sb, "{\n");
    api_sb_append_str(&sb, "  \"format\": \"cnf\",\n");
    api_sb_append_str(&sb, "  \"response\": {\n");

    api_sb_append_str(&sb, "    \"status\": \"");
    api_sb_append_str(&sb, status_str);
    api_sb_append_str(&sb, "\",\n");

    api_sb_append_str(&sb, "    \"status_code\": ");
    api_sb_append_int(&sb, status_code);
    api_sb_append_str(&sb, ",\n");

    api_sb_append_str(&sb, "    \"statistics\": {\n");
    api_sb_append_str(&sb, "      \"variables\": ");
    api_sb_append_int(&sb, max_var);
    api_sb_append_str(&sb, ",\n      \"clauses\": ");
    api_sb_append_int(&sb, clause_count);
    api_sb_append_str(&sb, ",\n      \"solve_time_ms\": ");
    api_sb_append_double(&sb, t_elapsed_ms);
    api_sb_append_str(&sb, "\n    },\n");

    // Raw Model Array
    api_sb_append_str(&sb, "    \"raw_model\": [");
    if (status_code == SODIUM_STATUS_SATISFIABLE) {
        bool first = true;
        for (int v = 1; v <= max_var; v++) {
            int lit_val = sodium_solver_get_value(solver, v);
            if (!first) api_sb_append_str(&sb, ", ");
            api_sb_append_int(&sb, lit_val);
            first = false;
        }
    }
    api_sb_append_str(&sb, "],\n");

    // Truth Assignment Dictionary
    api_sb_append_str(&sb, "    \"assignment\": {\n");
    if (status_code == SODIUM_STATUS_SATISFIABLE) {
        for (int v = 1; v <= max_var; v++) {
            int lit_val = sodium_solver_get_value(solver, v);
            api_sb_append_str(&sb, "      \"");
            api_sb_append_int(&sb, v);
            api_sb_append_str(&sb, "\": ");
            api_sb_append_str(&sb, (lit_val > 0) ? "true" : "false");
            if (v < max_var) api_sb_append_str(&sb, ",");
            api_sb_append_str(&sb, "\n");
        }
    }
    api_sb_append_str(&sb, "    },\n");

    // Failed assumptions core
    api_sb_append_str(&sb, "    \"failed_assumptions_core\": []\n");
    api_sb_append_str(&sb, "  }\n}");

    sodium_solver_destroy(solver);

    *out_json_response = sb.data;
    return status_code;
}

// ============================================================================
// SECTION 2: C++ DEAR IMGUI UI VERIFICATION BRIDGE
// ============================================================================

int sodium_api_verify_formula(
    const char* formula_text,
    int* out_status,
    int** out_model,
    int* out_max_var,
    double* out_time_ms
) {
    if (!formula_text || !out_status) return -1;

    LOG_INFO("API", "sodium_api_verify_formula invoked (Formula length: %zu bytes)", strlen(formula_text));
    double t_start = get_time_ms();
    SodiumSolver* solver = sodium_solver_create();

    int max_var = 0;
    parse_dimacs_into_solver(formula_text, solver, &max_var);

    int status = sodium_solver_solve(solver);
    double elapsed_ms = get_time_ms() - t_start;

    *out_status = status;
    if (out_time_ms) *out_time_ms = elapsed_ms;

    int final_var_count = sodium_solver_get_variable_count(solver);
    if (final_var_count > max_var) max_var = final_var_count;
    if (out_max_var) *out_max_var = max_var;

    LOG_INFO("SOLVER", "sodium_api_verify_formula completed: Status: %d, Vars: %d, Time: %.4f ms",
             status, max_var, elapsed_ms);

    if (out_model) {
        if (status == SODIUM_STATUS_SATISFIABLE && max_var > 0) {
            *out_model = (int*)malloc((max_var + 1) * sizeof(int));
            (*out_model)[0] = 0;
            for (int v = 1; v <= max_var; v++) {
                (*out_model)[v] = sodium_solver_get_value(solver, v);
            }
        } else {
            *out_model = NULL;
        }
    }

    sodium_solver_destroy(solver);
    return 0;
}

void sodium_api_interrupt(void) {
    // Interruption flag hook
}

void sodium_api_reset(void) {
    // Global reset hook
}
