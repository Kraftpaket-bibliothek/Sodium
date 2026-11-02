#ifndef SODIUM_NLP_API_H
#define SODIUM_NLP_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Sodium NLP Feature: Semantic Rule Translation & Prompt Engineering        */
/* ========================================================================= */

/**
 * @brief System prompt template that instructs language models to convert
 *        natural language requirements, contractual rules, and system specs
 *        into Sodium's CDCL JSON CNF schema.
 */
extern const char* const SODIUM_NLP_SYSTEM_PROMPT;

/**
 * @brief Constructs the Mode 2 NLP prompt payload:
 *        {
 *          "userPrompt": "<user_prompt>",
 *          "SystemPrompt": "<target JSON schema instructions for LLM conversion>"
 *        }
 *
 * @param user_prompt        The user-entered natural language prompt or specification.
 * @param out_payload_json   Pointer to dynamically allocated output JSON string.
 *                           Caller must free with sodium_nlp_free_string().
 * @return 0 on success, negative error code on failure.
 */
int sodium_api_build_nlp_payload(const char* user_prompt, char** out_payload_json);

/**
 * @brief Releases any string allocated by the Sodium NLP API.
 *
 * @param str                Pointer to string to free (safe with NULL).
 */
void sodium_nlp_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_NLP_API_H
