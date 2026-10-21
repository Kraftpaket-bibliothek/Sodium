#ifndef PARSER_H
#define PARSER_H
#include <stdbool.h>
typedef int Literal;

typedef struct {
    Literal *literals; 
    int count;         
} Clause;

typedef struct {
    Clause *clauses; 
    int num_clauses; 
    int num_variables; 
} Formula;

bool parse_dimacs(Formula *formula);

void cleanup_formula(Formula *formula);

#endif
