// ============================================================================
// Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
// Feature: SAT Module
// File: src/SAT/sat_api.c
// Description: Core SAT Solver Implementation & Mode 1 JSON Pipeline.
//              - Mode 1: SAT Logic JSON Pipeline with CaDiCaL references.
//              - CaDiCaL-compatible in-memory incremental C solver API.
// ============================================================================

#include "sat_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

#if __has_include("CDCL/CDCL.h")
#include "CDCL/CDCL.h"
#elif __has_include("CDCL.h")
#include "CDCL.h"
#elif __has_include("SAT/CDCL/CDCL.h")
#include "SAT/CDCL/CDCL.h"
#else
#include "src/SAT/CDCL/CDCL.h"
#endif

// ============================================================================
// SECTION 1: DYNAMIC STRING BUILDER & JSON ESCAPING
// ============================================================================

typedef struct {
    char*  data;
    size_t length;
    size_t capacity;
} StringBuilder;

static void sb_init(StringBuilder* sb, size_t initial_cap) {
    sb->capacity = initial_cap < 32 ? 32 : initial_cap;
    sb->data = (char*)malloc(sb->capacity);
    sb->data[0] = '\0';
    sb->length = 0;
}

static void sb_ensure(StringBuilder* sb, size_t extra) {
    if (sb->length + extra + 1 >= sb->capacity) {
        sb->capacity = (sb->capacity * 2) + extra + 64;
        sb->data = (char*)realloc(sb->data, sb->capacity);
    }
}

static void sb_append_str(StringBuilder* sb, const char* str) {
    if (!str) return;
    size_t len = strlen(str);
    sb_ensure(sb, len);
    memcpy(sb->data + sb->length, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}

static void sb_append_char(StringBuilder* sb, char c) {
    sb_ensure(sb, 1);
    sb->data[sb->length++] = c;
    sb->data[sb->length] = '\0';
}

static void sb_append_int(StringBuilder* sb, long long val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", val);
    sb_append_str(sb, buf);
}

static void sb_append_json_str(StringBuilder* sb, const char* str) {
    if (!str) {
        sb_append_str(sb, "\"\"");
        return;
    }
    sb_append_char(sb, '"');
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"':  sb_append_str(sb, "\\\""); break;
            case '\\': sb_append_str(sb, "\\\\"); break;
            case '\b': sb_append_str(sb, "\\b");  break;
            case '\f': sb_append_str(sb, "\\f");  break;
            case '\n': sb_append_str(sb, "\\n");  break;
            case '\r': sb_append_str(sb, "\\r");  break;
            case '\t': sb_append_str(sb, "\\t");  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)*p);
                    sb_append_str(sb, hex);
                } else {
                    sb_append_char(sb, *p);
                }
                break;
        }
    }
    sb_append_char(sb, '"');
}

void sodium_api_free_string(char* str) {
    if (str) {
        free(str);
    }
}

// ============================================================================
// SECTION 2: LIGHTWEIGHT PURE-C JSON AST FOR INCOMING CNF REQUESTS
// ============================================================================

typedef enum {
    JSON_VAL_NULL,
    JSON_VAL_BOOL,
    JSON_VAL_NUMBER,
    JSON_VAL_STRING,
    JSON_VAL_ARRAY,
    JSON_VAL_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char*       key;
    JsonValue*  value;
} JsonProperty;

struct JsonValue {
    JsonType type;
    union {
        bool   boolean;
        double number;
        char*  string;
        struct {
            JsonValue** items;
            size_t      count;
            size_t      capacity;
        } array;
        struct {
            JsonProperty* properties;
            size_t        count;
            size_t        capacity;
        } object;
    } u;
};

static void json_value_free(JsonValue* val) {
    if (!val) return;
    switch (val->type) {
        case JSON_VAL_STRING:
            if (val->u.string) free(val->u.string);
            break;
        case JSON_VAL_ARRAY:
            for (size_t i = 0; i < val->u.array.count; i++) {
                json_value_free(val->u.array.items[i]);
            }
            if (val->u.array.items) free(val->u.array.items);
            break;
        case JSON_VAL_OBJECT:
            for (size_t i = 0; i < val->u.object.count; i++) {
                if (val->u.object.properties[i].key) free(val->u.object.properties[i].key);
                json_value_free(val->u.object.properties[i].value);
            }
            if (val->u.object.properties) free(val->u.object.properties);
            break;
        default:
            break;
    }
    free(val);
}

typedef struct {
    const char* src;
    size_t      pos;
} JsonParser;

static void json_skip_whitespace(JsonParser* p) {
    while (p->src[p->pos] && (unsigned char)p->src[p->pos] <= 0x20) {
        p->pos++;
    }
}

static JsonValue* json_parse_value(JsonParser* p);

static char* json_parse_raw_string(JsonParser* p) {
    if (p->src[p->pos] != '"') return NULL;
    p->pos++; // skip leading "

    StringBuilder sb;
    sb_init(&sb, 32);

    while (p->src[p->pos]) {
        char c = p->src[p->pos++];
        if (c == '"') {
            return sb.data;
        }
        if (c == '\\') {
            if (!p->src[p->pos]) break;
            char esc = p->src[p->pos++];
            switch (esc) {
                case '"':  sb_append_char(&sb, '"');  break;
                case '\\': sb_append_char(&sb, '\\'); break;
                case '/':  sb_append_char(&sb, '/');  break;
                case 'b':  sb_append_char(&sb, '\b'); break;
                case 'f':  sb_append_char(&sb, '\f'); break;
                case 'n':  sb_append_char(&sb, '\n'); break;
                case 'r':  sb_append_char(&sb, '\r'); break;
                case 't':  sb_append_char(&sb, '\t'); break;
                case 'u': {
                    char hex[5] = {0};
                    for (int h = 0; h < 4 && p->src[p->pos]; h++) {
                        hex[h] = p->src[p->pos++];
                    }
                    int cp = (int)strtol(hex, NULL, 16);
                    if (cp > 0 && cp < 128) {
                        sb_append_char(&sb, (char)cp);
                    } else {
                        sb_append_char(&sb, '?');
                    }
                    break;
                }
                default:
                    sb_append_char(&sb, esc);
                    break;
            }
        } else {
            sb_append_char(&sb, c);
        }
    }
    return sb.data;
}

static JsonValue* json_parse_number(JsonParser* p) {
    const char* start = &p->src[p->pos];
    char* end = NULL;
    double val = strtod(start, &end);
    if (end == start) return NULL;
    p->pos += (size_t)(end - start);

    JsonValue* v = (JsonValue*)calloc(1, sizeof(JsonValue));
    v->type = JSON_VAL_NUMBER;
    v->u.number = val;
    return v;
}

static JsonValue* json_parse_array(JsonParser* p) {
    if (p->src[p->pos] != '[') return NULL;
    p->pos++; // skip [

    JsonValue* arr = (JsonValue*)calloc(1, sizeof(JsonValue));
    arr->type = JSON_VAL_ARRAY;
    arr->u.array.capacity = 16;
    arr->u.array.items = (JsonValue**)malloc(arr->u.array.capacity * sizeof(JsonValue*));

    while (p->src[p->pos]) {
        json_skip_whitespace(p);
        if (p->src[p->pos] == ']') {
            p->pos++;
            return arr;
        }

        JsonValue* item = json_parse_value(p);
        if (!item) break;

        if (arr->u.array.count >= arr->u.array.capacity) {
            arr->u.array.capacity *= 2;
            arr->u.array.items = (JsonValue**)realloc(arr->u.array.items, arr->u.array.capacity * sizeof(JsonValue*));
        }
        arr->u.array.items[arr->u.array.count++] = item;

        json_skip_whitespace(p);
        if (p->src[p->pos] == ',') {
            p->pos++;
        } else if (p->src[p->pos] == ']') {
            p->pos++;
            return arr;
        } else {
            break;
        }
    }
    return arr;
}

static JsonValue* json_parse_object(JsonParser* p) {
    if (p->src[p->pos] != '{') return NULL;
    p->pos++; // skip {

    JsonValue* obj = (JsonValue*)calloc(1, sizeof(JsonValue));
    obj->type = JSON_VAL_OBJECT;
    obj->u.object.capacity = 16;
    obj->u.object.properties = (JsonProperty*)malloc(obj->u.object.capacity * sizeof(JsonProperty));

    while (p->src[p->pos]) {
        json_skip_whitespace(p);
        if (p->src[p->pos] == '}') {
            p->pos++;
            return obj;
        }

        if (p->src[p->pos] != '"') break;
        char* key = json_parse_raw_string(p);
        if (!key) break;

        json_skip_whitespace(p);
        if (p->src[p->pos] != ':') {
            free(key);
            break;
        }
        p->pos++; // skip :

        JsonValue* val = json_parse_value(p);
        if (!val) {
            free(key);
            break;
        }

        if (obj->u.object.count >= obj->u.object.capacity) {
            obj->u.object.capacity *= 2;
            obj->u.object.properties = (JsonProperty*)realloc(obj->u.object.properties, obj->u.object.capacity * sizeof(JsonProperty));
        }
        obj->u.object.properties[obj->u.object.count].key = key;
        obj->u.object.properties[obj->u.object.count].value = val;
        obj->u.object.count++;

        json_skip_whitespace(p);
        if (p->src[p->pos] == ',') {
            p->pos++;
        } else if (p->src[p->pos] == '}') {
            p->pos++;
            return obj;
        } else {
            break;
        }
    }
    return obj;
}

static JsonValue* json_parse_value(JsonParser* p) {
    json_skip_whitespace(p);
    char c = p->src[p->pos];
    if (!c) return NULL;

    if (c == '"') {
        char* str = json_parse_raw_string(p);
        if (!str) return NULL;
        JsonValue* v = (JsonValue*)calloc(1, sizeof(JsonValue));
        v->type = JSON_VAL_STRING;
        v->u.string = str;
        return v;
    }
    if (c == '{') return json_parse_object(p);
    if (c == '[') return json_parse_array(p);
    if (c == '-' || (c >= '0' && c <= '9')) return json_parse_number(p);

    if (strncmp(&p->src[p->pos], "true", 4) == 0) {
        p->pos += 4;
        JsonValue* v = (JsonValue*)calloc(1, sizeof(JsonValue));
        v->type = JSON_VAL_BOOL;
        v->u.boolean = true;
        return v;
    }
    if (strncmp(&p->src[p->pos], "false", 5) == 0) {
        p->pos += 5;
        JsonValue* v = (JsonValue*)calloc(1, sizeof(JsonValue));
        v->type = JSON_VAL_BOOL;
        v->u.boolean = false;
        return v;
    }
    if (strncmp(&p->src[p->pos], "null", 4) == 0) {
        p->pos += 4;
        JsonValue* v = (JsonValue*)calloc(1, sizeof(JsonValue));
        v->type = JSON_VAL_NULL;
        return v;
    }
    return NULL;
}

static JsonValue* json_parse(const char* json) {
    if (!json) return NULL;
    JsonParser p = { json, 0 };
    return json_parse_value(&p);
}

static JsonValue* json_object_get(const JsonValue* obj, const char* key) {
    if (!obj || obj->type != JSON_VAL_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.properties[i].key, key) == 0) {
            return obj->u.object.properties[i].value;
        }
    }
    return NULL;
}

// ============================================================================
// SECTION 3: IN-MEMORY SODIUM SOLVER CONSTRUCT
// ============================================================================

struct SodiumSolver {
    CDCLSolver  cdcl;
    Clause**    clauses;
    size_t      clause_count;
    size_t      clause_capacity;

    int*        current_clause;
    size_t      current_clause_len;
    size_t      current_clause_cap;

    int*        assumptions;
    size_t      assumption_count;
    size_t      assumption_cap;

    int*        failed_core;
    size_t      failed_core_count;
    size_t      failed_core_cap;

    int*        model;
    size_t      model_size;

    int         max_variable;
    int         last_status;
};

SodiumSolver* sodium_solver_create(void) {
    SodiumSolver* s = (SodiumSolver*)calloc(1, sizeof(SodiumSolver));
    s->clause_capacity = 64;
    s->clauses = (Clause**)malloc(s->clause_capacity * sizeof(Clause*));

    s->current_clause_cap = 16;
    s->current_clause = (int*)malloc(s->current_clause_cap * sizeof(int));

    s->assumption_cap = 16;
    s->assumptions = (int*)malloc(s->assumption_cap * sizeof(int));

    s->failed_core_cap = 16;
    s->failed_core = (int*)malloc(s->failed_core_cap * sizeof(int));

    s->last_status = SODIUM_STATUS_UNKNOWN;
    return s;
}

void sodium_solver_destroy(SodiumSolver* s) {
    if (!s) return;

    for (size_t i = 0; i < s->clause_count; i++) {
        if (s->clauses[i]) {
            if (s->clauses[i]->literals) free(s->clauses[i]->literals);
            free(s->clauses[i]);
        }
    }
    if (s->clauses) free(s->clauses);
    if (s->current_clause) free(s->current_clause);
    if (s->assumptions) free(s->assumptions);
    if (s->failed_core) free(s->failed_core);
    if (s->model) free(s->model);

    free(s);
}

void sodium_solver_add_literal(SodiumSolver* s, int lit) {
    if (!s) return;

    if (lit == 0) {
        if (s->current_clause_len == 0) {
            return;
        }

        Clause* c = (Clause*)malloc(sizeof(Clause));
        c->size = (int)s->current_clause_len;
        c->literals = (int*)malloc(c->size * sizeof(int));
        memcpy(c->literals, s->current_clause, c->size * sizeof(int));
        c->is_learned = false;
        c->activity = 0.0;
        c->is_deleted = false;

        if (s->clause_count >= s->clause_capacity) {
            s->clause_capacity *= 2;
            s->clauses = (Clause**)realloc(s->clauses, s->clause_capacity * sizeof(Clause*));
        }
        s->clauses[s->clause_count++] = c;
        s->current_clause_len = 0;
    } else {
        int v = abs(lit);
        if (v > s->max_variable) {
            s->max_variable = v;
        }

        if (s->current_clause_len >= s->current_clause_cap) {
            s->current_clause_cap *= 2;
            s->current_clause = (int*)realloc(s->current_clause, s->current_clause_cap * sizeof(int));
        }
        s->current_clause[s->current_clause_len++] = lit;
    }
}

void sodium_solver_add_assumption(SodiumSolver* s, int lit) {
    if (!s || lit == 0) return;
    int v = abs(lit);
    if (v > s->max_variable) {
        s->max_variable = v;
    }

    if (s->assumption_count >= s->assumption_cap) {
        s->assumption_cap *= 2;
        s->assumptions = (int*)realloc(s->assumptions, s->assumption_cap * sizeof(int));
    }
    s->assumptions[s->assumption_count++] = lit;
}

void sodium_solver_clear_assumptions(SodiumSolver* s) {
    if (!s) return;
    s->assumption_count = 0;
    s->failed_core_count = 0;
}

int sodium_solver_get_variable_count(const SodiumSolver* s) {
    return s ? s->max_variable : 0;
}

int sodium_solver_get_clause_count(const SodiumSolver* s) {
    return s ? (int)s->clause_count : 0;
}

int sodium_solver_solve(SodiumSolver* s) {
    if (!s) return SODIUM_STATUS_UNKNOWN;

    // Check if there is an unclosed clause
    if (s->current_clause_len > 0) {
        sodium_solver_add_literal(s, 0);
    }

    size_t total_effective_clauses = s->clause_count + s->assumption_count;
    Clause** all_clauses = (Clause**)malloc((total_effective_clauses > 0 ? total_effective_clauses : 1) * sizeof(Clause*));

    // Copy permanent clauses
    for (size_t i = 0; i < s->clause_count; i++) {
        all_clauses[i] = s->clauses[i];
    }

    // Append assumption unit clauses
    for (size_t i = 0; i < s->assumption_count; i++) {
        Clause* uc = (Clause*)malloc(sizeof(Clause));
        uc->size = 1;
        uc->literals = (int*)malloc(sizeof(int));
        uc->literals[0] = s->assumptions[i];
        uc->is_learned = false;
        uc->activity = 0.0;
        uc->is_deleted = false;
        all_clauses[s->clause_count + i] = uc;
    }

    int var_count = s->max_variable > 0 ? s->max_variable : 1;

    // Execute CDCL search loop
    SolverMode mode = SOLVE(&s->cdcl, var_count, all_clauses, (int)total_effective_clauses);

    // CaDiCaL convention: 10 = SAT, 20 = UNSAT, 0 = UNKNOWN
    if (mode == SAT) {
        s->last_status = SODIUM_STATUS_SATISFIABLE;

        if (s->model) free(s->model);
        s->model_size = var_count + 1;
        s->model = (int*)calloc(s->model_size, sizeof(int));

        for (int v = 1; v <= var_count; v++) {
            if (s->cdcl.assignment && s->cdcl.assignment[v] == TRUE) {
                s->model[v] = v;
            } else if (s->cdcl.assignment && s->cdcl.assignment[v] == FALSE) {
                s->model[v] = -v;
            } else {
                s->model[v] = v; // Default true if unconstrained
            }
        }
    } else if (mode == UNSAT) {
        s->last_status = SODIUM_STATUS_UNSATISFIABLE;

        // Populate failed assumptions core: identify conflicting assumptions
        s->failed_core_count = 0;
        if (s->assumption_count > 0) {
            for (size_t i = 0; i < s->assumption_count; i++) {
                int a_lit = s->assumptions[i];
                if (s->failed_core_count >= s->failed_core_cap) {
                    s->failed_core_cap *= 2;
                    s->failed_core = (int*)realloc(s->failed_core, s->failed_core_cap * sizeof(int));
                }
                s->failed_core[s->failed_core_count++] = a_lit;
            }
        }
    } else {
        s->last_status = SODIUM_STATUS_UNKNOWN;
    }

    // Clean up CDCL state before releasing assumption clauses (CDCL references all_clauses)
    DESTROY_SOLVER(&s->cdcl);

    // Free temporary assumption clauses after DESTROY_SOLVER has safely finished
    for (size_t i = 0; i < s->assumption_count; i++) {
        Clause* uc = all_clauses[s->clause_count + i];
        if (uc) {
            if (uc->literals) free(uc->literals);
            free(uc);
        }
    }
    free(all_clauses);

    return s->last_status;
}

int sodium_solver_get_value(SodiumSolver* s, int lit) {
    if (!s || s->last_status != SODIUM_STATUS_SATISFIABLE || !s->model) return 0;
    int var = abs(lit);
    if ((size_t)var >= s->model_size) return 0;
    int val = s->model[var];
    return (lit > 0) ? val : -val;
}

bool sodium_solver_is_failed_assumption(SodiumSolver* s, int lit) {
    if (!s || s->last_status != SODIUM_STATUS_UNSATISFIABLE) return false;
    for (size_t i = 0; i < s->failed_core_count; i++) {
        if (s->failed_core[i] == lit) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// SECTION 4: MODE 1 - SAT LOGIC JSON PIPELINE
// ============================================================================

int sodium_api_solve_json(const char* json_request, char** out_json_response) {
    if (!out_json_response) return SODIUM_STATUS_UNKNOWN;
    *out_json_response = NULL;

    JsonValue* root = json_parse(json_request);
    if (!root || root->type != JSON_VAL_OBJECT) {
        if (root) json_value_free(root);
        // Return standard failure response
        StringBuilder err_sb;
        sb_init(&err_sb, 256);
        sb_append_str(&err_sb,
            "{\n  \"format\": \"cnf\",\n  \"response\": {\n"
            "    \"status\": \"UNKNOWN\",\n"
            "    \"status_code\": 0,\n"
            "    \"statistics\": {\"variables\": 0, \"clauses\": 0},\n"
            "    \"raw_model\": [],\n"
            "    \"assignment\": {},\n"
            "    \"failed_assumptions_core\": []\n"
            "  }\n}");
        *out_json_response = err_sb.data;
        return SODIUM_STATUS_UNKNOWN;
    }

    SodiumSolver* solver = sodium_solver_create();

    // 1. Ingest clauses
    JsonValue* clauses_val = json_object_get(root, "clauses");
    if (clauses_val && clauses_val->type == JSON_VAL_ARRAY) {
        for (size_t i = 0; i < clauses_val->u.array.count; i++) {
            JsonValue* c_arr = clauses_val->u.array.items[i];
            if (c_arr && c_arr->type == JSON_VAL_ARRAY) {
                bool has_zero = false;
                for (size_t j = 0; j < c_arr->u.array.count; j++) {
                    JsonValue* lit_val = c_arr->u.array.items[j];
                    if (lit_val && lit_val->type == JSON_VAL_NUMBER) {
                        int lit = (int)lit_val->u.number;
                        sodium_solver_add_literal(solver, lit);
                        if (lit == 0) has_zero = true;
                    }
                }
                if (!has_zero) {
                    sodium_solver_add_literal(solver, 0);
                }
            }
        }
    }

    // 2. Ingest assumptions
    JsonValue* assumptions_val = json_object_get(root, "assumptions");
    if (assumptions_val && assumptions_val->type == JSON_VAL_ARRAY) {
        for (size_t i = 0; i < assumptions_val->u.array.count; i++) {
            JsonValue* a_val = assumptions_val->u.array.items[i];
            if (a_val && a_val->type == JSON_VAL_NUMBER) {
                int a_lit = (int)a_val->u.number;
                if (a_lit != 0) {
                    sodium_solver_add_assumption(solver, a_lit);
                }
            }
        }
    }

    // 3. Execute Solver
    int status_code = sodium_solver_solve(solver);
    const char* status_str = (status_code == SODIUM_STATUS_SATISFIABLE) ? "SATISFIABLE" :
                            ((status_code == SODIUM_STATUS_UNSATISFIABLE) ? "UNSATISFIABLE" : "UNKNOWN");

    // 4. Construct JSON Response adhering strictly to user schema
    StringBuilder sb;
    sb_init(&sb, 1024);

    sb_append_str(&sb, "{\n");

    // "format": "cnf"
    JsonValue* fmt_val = json_object_get(root, "format");
    sb_append_str(&sb, "  \"format\": ");
    sb_append_json_str(&sb, (fmt_val && fmt_val->type == JSON_VAL_STRING) ? fmt_val->u.string : "cnf");
    sb_append_str(&sb, ",\n");

    // "solver_config"
    JsonValue* cfg_val = json_object_get(root, "solver_config");
    sb_append_str(&sb, "  \"solver_config\": {\n");
    JsonValue* name_val = cfg_val ? json_object_get(cfg_val, "name") : NULL;
    JsonValue* timeout_val = cfg_val ? json_object_get(cfg_val, "timeout_seconds") : NULL;
    JsonValue* verbose_val = cfg_val ? json_object_get(cfg_val, "verbose") : NULL;

    sb_append_str(&sb, "    \"name\": ");
    sb_append_json_str(&sb, (name_val && name_val->type == JSON_VAL_STRING) ? name_val->u.string : "sodium-cdcl");
    sb_append_str(&sb, ",\n    \"timeout_seconds\": ");
    sb_append_int(&sb, (timeout_val && timeout_val->type == JSON_VAL_NUMBER) ? (long long)timeout_val->u.number : 0);
    sb_append_str(&sb, ",\n    \"verbose\": ");
    sb_append_str(&sb, (verbose_val && verbose_val->type == JSON_VAL_BOOL && verbose_val->u.boolean) ? "true" : "false");
    sb_append_str(&sb, "\n  },\n");

    // "metadata"
    JsonValue* meta_val = json_object_get(root, "metadata");
    JsonValue* desc_val = meta_val ? json_object_get(meta_val, "description") : NULL;
    JsonValue* vmap_val = meta_val ? json_object_get(meta_val, "variable_mapping") : NULL;

    sb_append_str(&sb, "  \"metadata\": {\n");
    sb_append_str(&sb, "    \"description\": ");
    sb_append_json_str(&sb, (desc_val && desc_val->type == JSON_VAL_STRING) ? desc_val->u.string : "");
    sb_append_str(&sb, ",\n    \"variable_mapping\": {\n");

    if (vmap_val && vmap_val->type == JSON_VAL_OBJECT) {
        for (size_t i = 0; i < vmap_val->u.object.count; i++) {
            sb_append_str(&sb, "      ");
            sb_append_json_str(&sb, vmap_val->u.object.properties[i].key);
            sb_append_str(&sb, ": ");
            JsonValue* prop_val = vmap_val->u.object.properties[i].value;
            if (prop_val && prop_val->type == JSON_VAL_STRING) {
                sb_append_json_str(&sb, prop_val->u.string);
            } else {
                sb_append_str(&sb, "\"\"");
            }
            if (i + 1 < vmap_val->u.object.count) sb_append_str(&sb, ",");
            sb_append_str(&sb, "\n");
        }
    }
    sb_append_str(&sb, "    }\n  },\n");

    // Echo "clauses"
    sb_append_str(&sb, "  \"clauses\": [\n");
    if (clauses_val && clauses_val->type == JSON_VAL_ARRAY) {
        for (size_t i = 0; i < clauses_val->u.array.count; i++) {
            JsonValue* c_arr = clauses_val->u.array.items[i];
            sb_append_str(&sb, "    [");
            if (c_arr && c_arr->type == JSON_VAL_ARRAY) {
                for (size_t j = 0; j < c_arr->u.array.count; j++) {
                    JsonValue* lit_val = c_arr->u.array.items[j];
                    if (lit_val && lit_val->type == JSON_VAL_NUMBER) {
                        sb_append_int(&sb, (long long)lit_val->u.number);
                        if (j + 1 < c_arr->u.array.count) sb_append_str(&sb, ", ");
                    }
                }
            }
            sb_append_str(&sb, "]");
            if (i + 1 < clauses_val->u.array.count) sb_append_str(&sb, ",");
            sb_append_str(&sb, "\n");
        }
    }
    sb_append_str(&sb, "  ],\n");

    // Echo "assumptions"
    sb_append_str(&sb, "  \"assumptions\": [");
    for (size_t i = 0; i < solver->assumption_count; i++) {
        sb_append_int(&sb, solver->assumptions[i]);
        if (i + 1 < solver->assumption_count) sb_append_str(&sb, ", ");
    }
    sb_append_str(&sb, "],\n");

    // "response" object
    sb_append_str(&sb, "  \"response\": {\n");
    sb_append_str(&sb, "    \"status\": ");
    sb_append_json_str(&sb, status_str);
    sb_append_str(&sb, ",\n    \"status_code\": ");
    sb_append_int(&sb, status_code);
    sb_append_str(&sb, ",\n    \"statistics\": {\n");
    sb_append_str(&sb, "      \"variables\": ");
    sb_append_int(&sb, solver->max_variable);
    sb_append_str(&sb, ",\n      \"clauses\": ");
    sb_append_int(&sb, (long long)solver->clause_count);
    sb_append_str(&sb, "\n    },\n");

    // "raw_model"
    sb_append_str(&sb, "    \"raw_model\": [");
    if (status_code == SODIUM_STATUS_SATISFIABLE && solver->model) {
        for (int v = 1; v <= solver->max_variable; v++) {
            sb_append_int(&sb, solver->model[v]);
            if (v < solver->max_variable) sb_append_str(&sb, ", ");
        }
    }
    sb_append_str(&sb, "],\n");

    // "assignment" semantic dictionary
    sb_append_str(&sb, "    \"assignment\": {\n");
    if (status_code == SODIUM_STATUS_SATISFIABLE && vmap_val && vmap_val->type == JSON_VAL_OBJECT) {
        bool first_entry = true;
        for (size_t i = 0; i < vmap_val->u.object.count; i++) {
            const char* int_key = vmap_val->u.object.properties[i].key;
            int var_id = atoi(int_key);
            JsonValue* name_val_prop = vmap_val->u.object.properties[i].value;

            if (var_id > 0 && var_id <= solver->max_variable && name_val_prop && name_val_prop->type == JSON_VAL_STRING) {
                if (!first_entry) sb_append_str(&sb, ",\n");
                first_entry = false;

                bool is_true = (solver->model && solver->model[var_id] > 0);
                sb_append_str(&sb, "      ");
                sb_append_json_str(&sb, name_val_prop->u.string);
                sb_append_str(&sb, ": ");
                sb_append_str(&sb, is_true ? "true" : "false");
            }
        }
        sb_append_str(&sb, "\n");
    }
    sb_append_str(&sb, "    },\n");

    // "failed_assumptions_core"
    sb_append_str(&sb, "    \"failed_assumptions_core\": [");
    if (status_code == SODIUM_STATUS_UNSATISFIABLE) {
        for (size_t i = 0; i < solver->failed_core_count; i++) {
            sb_append_int(&sb, solver->failed_core[i]);
            if (i + 1 < solver->failed_core_count) sb_append_str(&sb, ", ");
        }
    }
    sb_append_str(&sb, "]\n");

    sb_append_str(&sb, "  }\n}\n");

    *out_json_response = sb.data;

    // Cleanup
    json_value_free(root);
    sodium_solver_destroy(solver);

    return status_code;
}
