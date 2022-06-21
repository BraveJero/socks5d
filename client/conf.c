#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "conf.h"
#include "mgmt_client_util.h"

static void
version(void) {
    fprintf(stderr, "Cliente de administración\n"
                    "ITBA Protocolos de Comunicación 2022/1 -- Grupo X\n"
                    "AQUI VA LA LICENCIA\n");
}

static void
usage(const char *progname) {
    fprintf(stderr,
        "Usage: %s [OPTION]... [TOKEN]\n"
        "\n"
        "   -L <conf addr>   Dirección de management.\n"
        "   -P <conf port>   Puerto de management.\n"
        "   -h               Imprime la ayuda y termina.\n"
        "   -v               Imprime información sobre la versión versión y termina.\n"
        "\n",
        progname);
}

bool parse_conf(const int argc, char **argv, struct mnmt_conf* mnmt_conf) {
    int c;
    opterr = 0, optind = 0;
    while (-1 != (c = getopt (argc, argv, ARGUMENTS))) {
        switch(c) {
            case 'h':
                usage(argv[0]);
                exit(0);
            case 'v':
                version();
                exit(0);
            case 'L':
                if (*optarg == '-') {
                    fprintf (stderr, "Option -L requires an argument.\n");
                    return false;
                }
                mnmt_conf->addr = optarg;
                break;
            case 'P':
                if (*optarg == '-') {
                    fprintf (stderr, "Option -P requires an argument.\n");
                    return false;
                }
                mnmt_conf->port = optarg;
                break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7':
                break;
            case ':':
                fprintf (stderr, "Option -%c requires an argument.\n", optopt);
                return false;
            default:
                fprintf(stderr, "Unknown argument %c.\n", optopt);
                return false;
        }
    }
    const char * env_token = getenv(TOKEN_ENV_VAR);
    if (argc - optind < 1 && env_token == NULL) {
        fprintf(stderr, "A token must be provided either as an environment variable or as an operand.\n");
        usage(argv[0]);
        return false;
    }
    mnmt_conf->token = (env_token) ? env_token : argv[optind++];
    return true;
}
