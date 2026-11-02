// ============================================================================
// Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
// File: src/API.c
// Description: Core Sodium Public API Implementation.
//              - Mode 1: SAT Logic JSON Pipeline with CaDiCaL references.
//              - Mode 2: NLP Reasoning Bridge (SystemPrompt & userPrompt schema).
//              - CaDiCaL-compatible in-memory incremental C solver API.
// ============================================================================

#include "API.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

#if __has_include("CDCL.h")
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

static void sb_free(StringBuilder* sb) {
    if (sb->data) {
        free(sb->data);
        sb->data = NULL;
    }
    sb->length = 0;
    sb->capacity = 0;
}

void sodium_api_free_string(char* str) {
    if (str) {
        free(str);
    }
}

// ============================================================================
// SECTION 2: LIGHTWEIGHT PURE-C JSON PARSER
// ============================================================================

typedef enum {
    JSON_VAL_NULL,
    JSON_VAL_BOOL,
    JSON_VAL_NUMBER,
    JSON_VAL_STRING,
    JSON_VAL_ARRAY,
    JSON_VAL_OBJECT
} JsonValueType;

typedef struct JsonValue JsonValue;

typedef struct {
    char*      key;
    JsonValue* value;
} JsonProperty;

struct JsonValue {
    JsonValueType type;
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

static JsonValue* json_value_new(JsonValueType type) {
    JsonValue* v = (JsonValue*)calloc(1, sizeof(JsonValue));
    v->type = type;
    return v;
}

static void json_value_free(JsonValue* v) {
    if (!v) return;
    switch (v->type) {
        case JSON_VAL_STRING:
            if (v->u.string) free(v->u.string);
            break;
        case JSON_VAL_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++) {
                json_value_free(v->u.array.items[i]);
            }
            if (v->u.array.items) free(v->u.array.items);
            break;
        case JSON_VAL_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                if (v->u.object.properties[i].key) free(v->u.object.properties[i].key);
                json_value_free(v->u.object.properties[i].value);
            }
            if (v->u.object.properties) free(v->u.object.properties);
            break;
        default:
            break;
    }
    free(v);
}

typedef struct {
    const char* src;
    size_t      pos;
} JsonLexer;

static void json_skip_ws(JsonLexer* lex) {
    while (lex->src[lex->pos] && (isspace((unsigned char)lex->src[lex->pos]) || lex->src[lex->pos] == '\0')) {
        lex->pos++;
    }
}

static char json_peek(JsonLexer* lex) {
    json_skip_ws(lex);
    return lex->src[lex->pos];
}

static char json_next(JsonLexer* lex) {
    json_skip_ws(lex);
    return lex->src[lex->pos++];
}

static char* json_parse_string(JsonLexer* lex) {
    if (json_next(lex) != '"') return NULL;
    StringBuilder sb;
    sb_init(&sb, 32);

    while (lex->src[lex->pos]) {
        char c = lex->src[lex->pos++];
        if (c == '"') {
            char* out = sb.data;
            return out;
        }
        if (c == '\\') {
            if (!lex->src[lex->pos]) break;
            char esc = lex->src[lex->pos++];
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
                    // Skip 4 hex digits for basic parsing
                    for (int h = 0; h < 4 && lex->src[lex->pos]; h++) lex->pos++;
                    sb_append_char(&sb, '?');
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
    sb_free(&sb);
    return NULL;
}

static JsonValue* json_parse_value(JsonLexer* lex);

static JsonValue* json_parse_array(JsonLexer* lex) {
    if (json_next(lex) != '[') return NULL;
    JsonValue* arr = json_value_new(JSON_VAL_ARRAY);
    arr->u.array.capacity = 8;
    arr->u.array.items = (JsonValue**)malloc(arr->u.array.capacity * sizeof(JsonValue*));
    arr->u.array.count = 0;

    if (json_peek(lex) == ']') {
        json_next(lex);
        return arr;
    }

    while (true) {
        JsonValue* item = json_parse_value(lex);
        if (!item) {
            json_value_free(arr);
            return NULL;
        }
        if (arr->u.array.count >= arr->u.array.capacity) {
            arr->u.array.capacity *= 2;
            arr->u.array.items = (JsonValue**)realloc(arr->u.array.items, arr->u.array.capacity * sizeof(JsonValue*));
        }
        arr->u.array.items[arr->u.array.count++] = item;

        char c = json_peek(lex);
        if (c == ']') {
            json_next(lex);
            break;
        }
        if (c == ',') {
            json_next(lex);
        } else {
            json_value_free(arr);
            return NULL;
        }
    }
    return arr;
}

static JsonValue* json_parse_object(JsonLexer* lex) {
    if (json_next(lex) != '{') return NULL;
    JsonValue* obj = json_value_new(JSON_VAL_OBJECT);
    obj->u.object.capacity = 8;
    obj->u.object.properties = (JsonProperty*)malloc(obj->u.object.capacity * sizeof(JsonProperty));
    obj->u.object.count = 0;

    if (json_peek(lex) == '}') {
        json_next(lex);
        return obj;
    }

    while (true) {
        json_skip_ws(lex);
        if (json_peek(lex) != '"') {
            json_value_free(obj);
            return NULL;
        }
        char* key = json_parse_string(lex);
        if (!key) {
            json_value_free(obj);
            return NULL;
        }
        if (json_next(lex) != ':') {
            free(key);
            json_value_free(obj);
            return NULL;
        }
        JsonValue* val = json_parse_value(lex);
        if (!val) {
            free(key);
            json_value_free(obj);
            return NULL;
        }

        if (obj->u.object.count >= obj->u.object.capacity) {
            obj->u.object.capacity *= 2;
            obj->u.object.properties = (JsonProperty*)realloc(obj->u.object.properties,
                obj->u.object.capacity * sizeof(JsonProperty));
        }
        obj->u.object.properties[obj->u.object.count].key = key;
        obj->u.object.properties[obj->u.object.count].value = val;
        obj->u.object.count++;

        char c = json_peek(lex);
        if (c == '}') {
            json_next(lex);
            break;
        }
        if (c == ',') {
            json_next(lex);
        } else {
            json_value_free(obj);
            return NULL;
        }
    }
    return obj;
}

static JsonValue* json_parse_value(JsonLexer* lex) {
    json_skip_ws(lex);
    char c = json_peek(lex);
    if (c == '{') return json_parse_object(lex);
    if (c == '[') return json_parse_array(lex);
    if (c == '"') {
        char* s = json_parse_string(lex);
        if (!s) return NULL;
        JsonValue* v = json_value_new(JSON_VAL_STRING);
        v->u.string = s;
        return v;
    }
    if (c == 't' || c == 'f') {
        bool b = (c == 't');
        size_t len = b ? 4 : 5;
        if (strncmp(lex->src + lex->pos, b ? "true" : "false", len) == 0) {
            lex->pos += len;
            JsonValue* v = json_value_new(JSON_VAL_BOOL);
            v->u.boolean = b;
            return v;
        }
        return NULL;
    }
    if (c == 'n') {
        if (strncmp(lex->src + lex->pos, "null", 4) == 0) {
            lex->pos += 4;
            return json_value_new(JSON_VAL_NULL);
        }
        return NULL;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        char* endptr = NULL;
        double num = strtod(lex->src + lex->pos, &endptr);
        if (endptr == lex->src + lex->pos) return NULL;
        lex->pos = (size_t)(endptr - lex->src);
        JsonValue* v = json_value_new(JSON_VAL_NUMBER);
        v->u.number = num;
        return v;
    }
    return NULL;
}

static JsonValue* json_parse(const char* text) {
    if (!text) return NULL;
    JsonLexer lex = { text, 0 };
    return json_parse_value(&lex);
}

static JsonValue* json_object_get(const JsonValue* obj, const char* key) {
    if (!obj || obj->type != JSON_VAL_OBJECT) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.properties[i].key, key) == 0) {
            return obj->u.object.properties[i].value;
        }
    }
    return NULL;
}

// ============================================================================
// SECTION 3: IN-MEMORY SOLVER IMPLEMENTATION (CaDiCaL Reference Semantics)
// ============================================================================

struct SodiumSolver {
    int max_variable;

    // Clause accumulation buffer
    int*   current_clause;
    size_t current_clause_size;
    size_t current_clause_cap;

    // Base clause database
    Clause** clauses;
    size_t   clause_count;
    size_t   clause_capacity;

    // Incremental assumption literals
    int*   assumptions;
    size_t assumption_count;
    size_t assumption_cap;

    // Failed assumptions core (on UNSAT)
    int*   failed_core;
    size_t failed_core_count;
    size_t failed_core_cap;

    // Computed truth model (1-indexed: +v, -v, or 0)
    int*   model;
    size_t model_size;

    // CDCL Engine instance
    CDCLSolver cdcl;
    int last_status;
};

SodiumSolver* sodium_solver_create(void) {
    SodiumSolver* s = (SodiumSolver*)calloc(1, sizeof(SodiumSolver));
    s->current_clause_cap = 16;
    s->current_clause = (int*)malloc(s->current_clause_cap * sizeof(int));

    s->clause_capacity = 32;
    s->clauses = (Clause**)malloc(s->clause_capacity * sizeof(Clause*));

    s->assumption_cap = 16;
    s->assumptions = (int*)malloc(s->assumption_cap * sizeof(int));

    s->failed_core_cap = 16;
    s->failed_core = (int*)malloc(s->failed_core_cap * sizeof(int));

    s->last_status = SODIUM_STATUS_UNKNOWN;
    return s;
}

void sodium_solver_destroy(SodiumSolver* s) {
    if (!s) return;

    if (s->current_clause) free(s->current_clause);

    if (s->clauses) {
        for (size_t i = 0; i < s->clause_count; i++) {
            Clause* c = s->clauses[i];
            if (c) {
                if (c->literals) free(c->literals);
                free(c);
            }
        }
        free(s->clauses);
    }

    if (s->assumptions) free(s->assumptions);
    if (s->failed_core) free(s->failed_core);
    if (s->model) free(s->model);

    free(s);
}

void sodium_solver_add_literal(SodiumSolver* s, int lit) {
    if (!s) return;

    if (lit != 0) {
        if (abs(lit) > s->max_variable) {
            s->max_variable = abs(lit);
        }
        if (s->current_clause_size >= s->current_clause_cap) {
            s->current_clause_cap *= 2;
            s->current_clause = (int*)realloc(s->current_clause, s->current_clause_cap * sizeof(int));
        }
        s->current_clause[s->current_clause_size++] = lit;
    } else {
        // 0 finishes the current clause
        Clause* c = (Clause*)malloc(sizeof(Clause));
        c->size = (int)s->current_clause_size;
        c->literals = (int*)malloc(c->size * sizeof(int));
        if (c->size > 0) {
            memcpy(c->literals, s->current_clause, c->size * sizeof(int));
        }
        c->is_learned = false;
        c->activity = 0.0;
        c->is_deleted = false;

        if (s->clause_count >= s->clause_capacity) {
            s->clause_capacity *= 2;
            s->clauses = (Clause**)realloc(s->clauses, s->clause_capacity * sizeof(Clause*));
        }
        s->clauses[s->clause_count++] = c;
        s->current_clause_size = 0;
    }
}

void sodium_solver_add_assumption(SodiumSolver* s, int lit) {
    if (!s || lit == 0) return;
    if (abs(lit) > s->max_variable) {
        s->max_variable = abs(lit);
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

int sodium_solver_solve(SodiumSolver* s) {
    if (!s) return SODIUM_STATUS_UNKNOWN;

    // Reset failed core
    s->failed_core_count = 0;

    // If a clause was left unterminated without a trailing 0, finalize it
    if (s->current_clause_size > 0) {
        sodium_solver_add_literal(s, 0);
    }

    // Build effective clause list including temporary assumptions as unit clauses
    size_t total_effective_clauses = s->clause_count + s->assumption_count;
    Clause** all_clauses = (Clause**)malloc(total_effective_clauses * sizeof(Clause*));

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
                // Check if assumption variable was involved in the conflict or falsified
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
        if (s->failed_core[i] == lit) return true;
    }
    return false;
}

int sodium_solver_get_variable_count(const SodiumSolver* s) {
    return s ? s->max_variable : 0;
}

int sodium_solver_get_clause_count(const SodiumSolver* s) {
    return s ? (int)s->clause_count : 0;
}

// ============================================================================
// SECTION 4: MODE 2 - NLP PROMPT SCHEMA GENERATOR
// ============================================================================

static const char* SODIUM_NLP_SYSTEM_PROMPT =
"You are the formal logic compiler for the Sodium Automated Reasoning Engine.\n"
"Your task is to transform the user's natural language requirements into a strictly valid CNF JSON object.\n\n"
"Output ONLY a single raw JSON object with NO markdown code fences (no ```json), NO conversational preamble, and NO trailing explanation.\n\n"
"The output MUST conform exactly to this JSON schema:\n"
"{\n"
"  \"format\": \"cnf\",\n"
"  \"solver_config\": {\n"
"    \"name\": \"sodium-cdcl\",\n"
"    \"timeout_seconds\": 10,\n"
"    \"verbose\": false\n"
"  },\n"
"  \"metadata\": {\n"
"    \"description\": \"<Brief summary of the verified requirements>\",\n"
"    \"variable_mapping\": {\n"
"      \"1\": \"<Atomic_Condition_Or_Component_1>\",\n"
"      \"2\": \"<Atomic_Condition_Or_Component_2>\"\n"
"    }\n"
"  },\n"
"  \"clauses\": [\n"
"    [ <signed_integer_literals>, 0 ]\n"
"  ],\n"
"  \"assumptions\": [ <optional_assumed_literals> ]\n"
"}\n\n"
"Rules for Compilation:\n"
"1. Identify all distinct atomic boolean propositions and assign each a 1-based integer ID in 'variable_mapping' (e.g. \"1\": \"Power_Active\", \"2\": \"Backup_Online\").\n"
"2. Translate all requirements into Conjunctive Normal Form (CNF) clauses:\n"
"   - Positive integer N represents proposition N is TRUE.\n"
"   - Negative integer -N represents proposition N is FALSE.\n"
"   - Every clause array MUST end with 0 as a terminator (e.g. [-1, 2, 0]).\n"
"3. Standard Logic Equivalences:\n"
"   - 'IF A THEN B' becomes (NOT A OR B) -> [-A, B, 0]\n"
"   - 'A and B cannot be active together' becomes (NOT A OR NOT B) -> [-A, -B, 0]\n"
"   - 'Choose exactly ONE of A or B' becomes [[A, B, 0], [-A, -B, 0]]\n"
"   - 'Always require A' becomes [A, 0]\n"
"   - 'Require A unless B' becomes (A OR B) -> [A, B, 0]\n"
"4. Put any specific hypothetical condition or what-if query in the 'assumptions' array.\n";

int sodium_api_build_nlp_payload(const char* user_prompt, char** out_payload_json) {
    if (!out_payload_json) return -1;
    *out_payload_json = NULL;

    if (!user_prompt) user_prompt = "";

    StringBuilder sb;
    sb_init(&sb, 1024);

    sb_append_str(&sb, "{\n  \"userPrompt\": ");
    sb_append_json_str(&sb, user_prompt);
    sb_append_str(&sb, ",\n  \"SystemPrompt\": ");
    sb_append_json_str(&sb, SODIUM_NLP_SYSTEM_PROMPT);
    sb_append_str(&sb, "\n}\n");

    *out_payload_json = sb.data;
    return 0;
}

// ============================================================================
// SECTION 5: MODE 1 - SAT LOGIC JSON PIPELINE
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
