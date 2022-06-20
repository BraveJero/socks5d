#ifndef __CONF_H__
#define __CONF_H__

#include <stdbool.h>

#include "mgmt_client_util.h"

typedef struct mnmt_conf {
    MgmtCommands cmd;
    const char * addr;
    const char * port;
    char * token;
} mnmt_conf;

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * mnmt-conf con defaults o la seleccion humana.
 */
bool parse_conf(const int argc, char **argv, struct mnmt_conf* mnmt_conf);

#endif
