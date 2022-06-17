#pragma once

#include <stdbool.h>

#define MAX_TOKEN_LENGTH 32
#define MAX_TOKENS 10

// Agrega un token a la lista de tokens
bool add_token(const char *token);

// Chequea si un token existe en la lista de tokens
bool check_token(const char *token);
