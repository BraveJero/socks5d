#ifndef __MGMT_CLIENT_UTIL_H__
#define __MGMT_CLIENT_UTIL_H__

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <netdb.h>

typedef enum {
    CMD_CAPA = 0,
    CMD_PASS,
    CMD_STATS,
    CMD_USERS,
    CMD_BUFFSIZE,
    CMD_DISSECTOR_STATUS,
    CMD_SET_BUFFSIZE,
    CMD_SET_DISSECTOR_STATUS,
} MgmtCommands;

int tcpClientSocket(const char *host, const char *service);
bool read_hello(int sock);
bool authenticate(int sock, const char *token);
bool set_buffsize(int sock, size_t size);
bool set_dissector_status(int sock, const char *status);

#endif