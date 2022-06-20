#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "pop3_sniffer.h"
#include "state.h"

static const char *pop3_user_str = "USER ";
static const char *pop3_pass_str = "PASS ";

PopState unknown_cmd(pop3_parser *p, uint8_t c) {
    if(c == '\n') {
        return p->user_complete? POP3_PASS_CMD : POP3_USER_CMD;
    }
    return POP3_UNKOWN_CMD;
}

PopState user_cmd(pop3_parser *p, uint8_t c) {
    if (tolower(c) == tolower(pop3_user_str[p->read_ptr])) {
        p->read_ptr++;
        // Detecte "USER " en el buffer. Leo el argumento
        if(pop3_user_str[p->read_ptr] == '\0') {
            p->read_ptr = 0;
            return POP3_FILL_USER;
        }
        //
        return POP3_USER_CMD;
    }
    return unknown_cmd(p, c);
}

PopState fill_user(pop3_parser *p, uint8_t c) {
    if(c == '\n')  {
        p->user[p->write_ptr] = '\0';
        p->write_ptr = 0;
        p->user_complete = true;
        return POP3_PASS_CMD;
    }
    if(p->write_ptr + 1 == MAX_CREDENTIALS_LEN) return POP3_UNKOWN_CMD;
    p->user[p->write_ptr++] = (char) c;
    return POP3_FILL_USER;
}

PopState pass_cmd(pop3_parser *p, uint8_t c) {
    if (tolower(c) == tolower(pop3_pass_str[p->read_ptr])) {
        p->read_ptr++;
        // Detecte "PASS " en el buffer. Leo el argumento
        if(pop3_pass_str[p->read_ptr] == '\0') {
            p->read_ptr = 0;
            return POP3_FILL_PASS;
        }
        return POP3_PASS_CMD;
    }
    return unknown_cmd(p, c);
}

PopState fill_pass(pop3_parser *p, uint8_t c) {
    if(c == '\n')  {
        p->pass[p->write_ptr] = '\0';
        p->write_ptr = 0;
        p->password_complete = true;
        return POP3_PASS_CMD;
    }
    if(p->write_ptr + 1 == MAX_CREDENTIALS_LEN) return POP3_UNKOWN_CMD;
    p->pass[p->write_ptr++] = (char) c;
    return POP3_FILL_PASS;
}

void init_parser(pop3_parser *p) {
    p->state = POP3_USER_CMD;
    p->read_ptr = p->write_ptr = 0;
    p->password_complete = p->user_complete = false;
}

void pop3_consume(pop3_parser *p) {
    while(buffer_can_read(&(p->buf)) && !p->password_complete) {
        uint8_t c = buffer_read(&(p->buf));
        switch (p->state) {
            case POP3_USER_CMD: {
                p->state = user_cmd(p, c);
                break;
            }
            case POP3_FILL_USER: {
                p->state = fill_user(p, c);
                break;
            }
            case POP3_PASS_CMD:
                p->state = pass_cmd(p, c);
                break;
            case POP3_FILL_PASS:
                p->state = fill_pass(p, c);
                break;
            case POP3_UNKOWN_CMD:
                p->state = unknown_cmd(p, c);
                break;
        }
    }
}

// Si retorna true, es que en user y pass estan cargadas un par de credenciales.
bool pop3_parse(pop3_parser *p, buffer *request) {
    p->buf.data = request->data;
    p->buf.read = request->read;
    p->buf.write = request->write;
    p->buf.limit = request->limit;

    pop3_consume(p);

    bool ans = p->user_complete && p->password_complete;
    if(ans) {
        // Me vuelvo a preparar para sniffear
        init_parser(p);
    }

    return ans;
}
