#include <stdio.h>
#include <stddef.h>

#include "mgmt_client_util.h"
#include "conf.h"

int main(int argc, char *argv[]) {
    int sock = 0, exit_status = 0;
    char *err_msg, *success_msg = "";
    mnmt_conf conf = {
        .addr = "127.0.0.1", // default address
        .port = "8080", // default port
        .token = NULL, // default token
    };

    if (!parse_conf(argc, argv, &conf)) {
        err_msg = "Error parsing configuration from arguments";
        exit_status = 1;
        goto finally;
    }

    if((conf.sock = tcpClientSocket(conf.addr, conf.port)) < 0) {
        err_msg = "Error creating sock with server";
        exit_status = 1;
        goto finally;
    }

    if(!read_hello(conf.sock)) {
        err_msg = "Error in server greeting";
        exit_status = 1;
        goto finally;
    }

    if(!authenticate(conf.sock, conf.token)) {
        err_msg = "Could not authenticate in server";
        exit_status = 1;
        goto finally;
    }

    int c;
    opterr = 0, optind = 0;
    while (-1 != (c = getopt (argc, argv, ARGUMENTS))) {
        switch(c) {
            // CAPA, STATS, USERS, BUFFSIZE, DISSECTOR_STATUS, SET-BUFFISZE, SET-DISSECTOR-STATUS
            case '0':
                capa(conf.sock);
                break;
            case '1':
                stats(conf.sock);
                break;
        }
    }

    printf("%s\n", success_msg);
    return 0;

finally:
    if(exit_status) fprintf(stderr, "%s\n", err_msg);
    if(errno) perror("");
    close(sock);
    return exit_status;
}
