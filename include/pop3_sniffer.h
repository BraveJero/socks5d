#pragma once

#include <stdint.h>
#include "buffer.h"

#define MAX_CREDENTIALS_LEN 40

typedef enum PopState {
    POP3_USER_CMD,
    POP3_FILL_USER,
    POP3_PASS_CMD,
    POP3_FILL_PASS,
    POP3_UNKOWN_CMD,
} PopState;

typedef struct pop3_parser {
    PopState state;
    bool password_complete, user_complete;
    char user[MAX_CREDENTIALS_LEN], pass[MAX_CREDENTIALS_LEN];
    buffer buf;
    uint16_t read_ptr, write_ptr;
} pop3_parser;

void init_parser(pop3_parser *p);

// Si retorna true, es que en user y pass estan cargadas un par de credenciales.
bool pop3_parse(pop3_parser *p, buffer *request);
