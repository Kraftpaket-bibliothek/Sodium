// ============================================================================
// Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
// Feature: NLP Module
// File: src/NLP/nlp_api.c
// Description: Natural Language Processing reasoning bridge.
//              Constructs userPrompt and SystemPrompt payloads for LLM
//              conversion into Sodium's CDCL JSON schema.
// ============================================================================

#include "nlp_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const SODIUM_NLP_SYSTEM_PROMPT =
"You are Sodium's Neuro-Symbolic Logic Translator. Your task is to rigorously extract\n"
"all logical entities, constraints, dependencies, and assumptions from the user's\n"
"natural language specification and output a single, strictly valid JSON document\n"
"in Conjunctive Normal Form (CNF) ready for processing by Sodium's CDCL engine.\n\n"
"TARGET JSON FORMAT (OUTPUT ONLY THIS JSON SCHEMA):\n"
"{\n"
"  \"format\": \"cnf\",\n"
"  \"solver_config\": {\n"
"    \"name\": \"sodium-cdcl\",\n"
"    \"timeout_seconds\": 10,\n"
"    \"verbose\": false\n"
"  },\n"
"  \"metadata\": {\n"
"    \"description\": \"<Brief summary of logical domain>\",\n"
"    \"variable_mapping\": {\n"
"      \"1\": \"<DescriptiveVariableName1>\",\n"
"      \"2\": \"<DescriptiveVariableName2>\"\n"
"    }\n"
"  },\n"
"  \"clauses\": [\n"
"    [ 1, 2, 0 ],\n"
"    [ -1, 3, 0 ]\n"
"  ],\n"
"  \"assumptions\": [\n"
"    1\n"
"  ]\n"
"}\n\n"
"TRANSLATION RULES:\n"
"1. Every distinct atomic proposition must be assigned a unique positive integer starting from 1.\n"
"2. In 'clauses':\n"
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

typedef struct {
    char*  data;
    size_t length;
    size_t capacity;
} NlpStringBuilder;

static void nlp_sb_init(NlpStringBuilder* sb, size_t initial_cap) {
    sb->capacity = initial_cap < 32 ? 32 : initial_cap;
    sb->data = (char*)malloc(sb->capacity);
    sb->data[0] = '\0';
    sb->length = 0;
}

static void nlp_sb_ensure(NlpStringBuilder* sb, size_t extra) {
    if (sb->length + extra + 1 >= sb->capacity) {
        sb->capacity = (sb->capacity * 2) + extra + 64;
        sb->data = (char*)realloc(sb->data, sb->capacity);
    }
}

static void nlp_sb_append_str(NlpStringBuilder* sb, const char* str) {
    if (!str) return;
    size_t len = strlen(str);
    nlp_sb_ensure(sb, len);
    memcpy(sb->data + sb->length, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}

static void nlp_sb_append_char(NlpStringBuilder* sb, char c) {
    nlp_sb_ensure(sb, 1);
    sb->data[sb->length++] = c;
    sb->data[sb->length] = '\0';
}

static void nlp_sb_append_json_str(NlpStringBuilder* sb, const char* str) {
    if (!str) {
        nlp_sb_append_str(sb, "\"\"");
        return;
    }
    nlp_sb_append_char(sb, '"');
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"':  nlp_sb_append_str(sb, "\\\""); break;
            case '\\': nlp_sb_append_str(sb, "\\\\"); break;
            case '\b': nlp_sb_append_str(sb, "\\b");  break;
            case '\f': nlp_sb_append_str(sb, "\\f");  break;
            case '\n': nlp_sb_append_str(sb, "\\n");  break;
            case '\r': nlp_sb_append_str(sb, "\\r");  break;
            case '\t': nlp_sb_append_str(sb, "\\t");  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)*p);
                    nlp_sb_append_str(sb, hex);
                } else {
                    nlp_sb_append_char(sb, *p);
                }
                break;
        }
    }
    nlp_sb_append_char(sb, '"');
}

int sodium_api_build_nlp_payload(const char* user_prompt, char** out_payload_json) {
    if (!out_payload_json) return -1;
    *out_payload_json = NULL;

    if (!user_prompt) user_prompt = "";

    NlpStringBuilder sb;
    nlp_sb_init(&sb, 1024);

    nlp_sb_append_str(&sb, "{\n  \"userPrompt\": ");
    nlp_sb_append_json_str(&sb, user_prompt);
    nlp_sb_append_str(&sb, ",\n  \"SystemPrompt\": ");
    nlp_sb_append_json_str(&sb, SODIUM_NLP_SYSTEM_PROMPT);
    nlp_sb_append_str(&sb, "\n}\n");

    *out_payload_json = sb.data;
    return 0;
}

void sodium_nlp_free_string(char* str) {
    if (str) {
        free(str);
    }
}
