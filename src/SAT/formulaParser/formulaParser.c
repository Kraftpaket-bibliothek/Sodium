#include "formulaParser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#define MAX_LINE_LENGTH 1024
static char *read_line(); 
static bool parse_clause_line(char *line, Clause *current_clause);

// --- Memory Management Implementations (Private) ---
static void add_literal(Clause *clause, Literal lit) {
    clause->count++;
    clause->literals = (Literal *)realloc(clause->literals, clause->count * sizeof(Literal));
    if (clause->literals == NULL) {
        perror("Failed to reallocate memory for clause literals");
        exit(EXIT_FAILURE); // Fatal error during parsing
    }
    clause->literals[clause->count - 1] = lit;
}

static bool finalize_clause(Formula *formula, Clause *current_clause) {
    formula->num_clauses++;
    formula->clauses = (Clause *)realloc(formula->clauses, formula->num_clauses * sizeof(Clause));
    if (formula->clauses == NULL) {
        perror("Failed to reallocate memory for formula clauses");
        exit(EXIT_FAILURE); // Fatal error during parsing
    }
    formula->clauses[formula->num_clauses - 1] = *current_clause;
    return true;
}

void cleanup_formula(Formula *formula) {
    for (int i = 0; i < formula->num_clauses; ++i) {
        free(formula->clauses[i].literals);
    }
    free(formula->clauses); // Free the main array of clauses.
}

static char *read_line() {
    static char line_buffer[MAX_LINE_LENGTH]; 

    if (fgets(line_buffer, MAX_LINE_LENGTH, stdin) == NULL) {
        return NULL; // EOF reached
    }

    size_t len = strlen(line_buffer);
    if (len > 0 && line_buffer[len - 1] == '\n') {
        line_buffer[len - 1] = '\0';
    } else if (len > 0 && line_buffer[len - 1] == '\r') {
        line_buffer[len - 1] = '\0';
    }

    return line_buffer;
}

static bool parse_clause_line(char *line, Clause *current_clause) {
    char *line_copy = strdup(line); 
    if (!line_copy) return false;

    char *token = strtok(line_copy, " \t\n");
    bool clause_complete = false;

    while (token != NULL) {
        Literal literal = atoi(token);
        if (literal == 0) {
            clause_complete = true;
            break; // End of clause found (DIMACS convention)
        }
        add_literal(current_clause, literal);
        token = strtok(NULL, " \t\n");
    }

    free(line_copy); 
    return clause_complete;
}

bool parse_dimacs(Formula *formula) {
    Clause current_clause = {NULL, 0}; // Temporary clause buffer

    while (true) {
        char *line = read_line();
        if (line == NULL) break; // EOF reached
        if (strncmp(line, "c", 1) == 0 || line[0] == '\r') {
            continue; 
        }
        if (strncmp(line, "p cnf", 5) == 0) {
            char *token = strtok(line, " \t\n"); // Token 1: 'p'
            if (token == NULL || strcmp(token, "p") != 0) continue;

            // Token 2: 'cnf'
            token = strtok(NULL, " \t\n");
            if (token == NULL || strcmp(token, "cnf") != 0) continue;

            // Token 3: M (number of variables)
            token = strtok(NULL, " \t\n");
            if (token != NULL) {
                formula->num_variables = atoi(token); // Successfully read M
            } else {
                fprintf(stderr, "Error: Missing variable count (M) in problem line.\n");
                return false; // Fatal parsing error
            }

            // Token 4: N (number of clauses) - Read but not strictly necessary for dynamic parsing
            strtok(NULL, " \t\n"); 
            continue; // Successfully processed the header line
        }

        // Clause Line Check (must end with '0')
        if (parse_clause_line(line, &current_clause)) {
            // If the clause was successfully terminated by '0', finalize and store it.
            if (current_clause.count > 0) {
                finalize_clause(formula, &current_clause);
            }
        }
    }

    return true; 
}
