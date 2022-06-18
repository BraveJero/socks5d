#include "mgmt_client_util.h"

#include <stdio.h>
#include <stddef.h>

const char *token = "password";
MgmtCommands cmd = CMD_SET_DISSECTOR_STATUS;

int main(int argc, char *argv[]){
    int sock, exit_status = 0;
    char *err_msg, *success_msg = "";
    if((sock = tcpClientSocket("localhost", "8080")) < 0) {
        err_msg = "Error creating sock with server";
        exit_status = 1;
        goto finally;
    }

    if(!read_hello(sock)) {
        err_msg = "Error in server greeting";
        exit_status = 1;
        goto finally;
    }

    if(!authenticate(sock, token)) {
        err_msg = "Could not authenticate in server";
        exit_status = 1;
        goto finally;
    }

    switch(cmd) {
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

finally:
    if(exit_status) fprintf(stderr, "%s\n", err_msg);
    if(errno) perror("");
    close(sock);
    return exit_status;
}
