#include "tokens.h"
#include <string.h>
#include <ctype.h>

typedef char token[MAX_TOKEN_LENGTH];

static token tokens[MAX_TOKENS];
static size_t token_count;

static bool check_valid(const char *token) {
    int i;
    for(i = 0; token[i] && i < MAX_TOKEN_LENGTH; i++) {
        if(!isprint(token[i])) {
            return false;
        }
    }
    return i < MAX_TOKEN_LENGTH;
}

bool add_token(const char *token) {
    if (token_count >= MAX_TOKENS || !check_valid(token)) {
        return false;
    }
    strncpy(tokens[token_count], token, MAX_TOKEN_LENGTH);
    token_count++;
    return true;
}

bool check_token(const char *token) {
    for(size_t i = 0; i < token_count; i++) {
        if(strcmp(tokens[i], token) == 0) {
            return true;
        }
    }
    return false;
}

size_t get_token_count() {
    return token_count;
}
