#ifndef __CONF_H__
#define __CONF_H__

#include <stdbool.h>

#include "mgmt_client_util.h"

#define TOKEN_ENV_VAR   "TOKEN"

// CAPA, STATS, USERS, BUFFSIZE, DISSECTOR_STATUS, SET-BUFFISZE, SETT-DISSECTOR-STATUS, ADD-USER
#define COMMAND_ARGUMENTS "012345:6:7:"
#define CONF_ARGUMENTS  ":hvL:P:"
#define ARGUMENTS   CONF_ARGUMENTS COMMAND_ARGUMENTS

typedef struct mnmt_conf {
    const char * addr;
    const char * port;
    const char * token;
    int sock;
} mnmt_conf;

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * mnmt-conf con defaults o la seleccion humana.
 */
bool parse_conf(const int argc, char **argv, struct mnmt_conf* mnmt_conf);

#endif
