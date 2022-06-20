#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "conf.h"

#define OPTIONAL_ARGUMENTS  "hvL:P:"

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
    opterr = 0;
    while (-1 != (c = getopt (argc, argv, OPTIONAL_ARGUMENTS))) {
        switch(c) {
            case 'h':
                usage(argv[0]);
                exit(0);
            case 'v':
                version();
                exit(0);
            case 'L':
                mnmt_conf->addr = optarg;
                break;
            case 'P':
                mnmt_conf->port = optarg;
                break;
            case '?':
                if (optopt == ':') {
                    fprintf (stderr, "Option -%c requires an argument.\n", optopt);
                    return false;
                }
                break;
        }
    }
    if (argc - optind < 1) {
        fprintf(stderr, "A token must be provided.\n");
        usage(argv[0]);
        return false;
    }
    mnmt_conf->token = argv[optind++];
    return true;
}
