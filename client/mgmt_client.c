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

    switch(conf.cmd) {
        case CMD_STATS:
            break;
        case CMD_CAPA:
            break;
        case CMD_USERS:
            break;
        case CMD_BUFFSIZE:
            break;
        case CMD_DISSECTOR_STATUS:
            break;
        case CMD_SET_BUFFSIZE: {
            size_t size = 1024;
            if (set_buffsize(sock, size)) {
                success_msg = "Buffsize updated";
            } else {
                err_msg = "Error updating buffsize";
                exit_status = 1;
            }
            break;
        }
        case CMD_SET_DISSECTOR_STATUS: {
            const char *status = "on";
            if (set_dissector_status(sock, status)) {
                success_msg = "Dissector status updated";
            } else {
                err_msg = "Error updating dissector exit_status";
                exit_status = 1;
                goto finally;
            }
            break;
        }
        default: {
            err_msg = "Unknown command";
            exit_status = 1;
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
