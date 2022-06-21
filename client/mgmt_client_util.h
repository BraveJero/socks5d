#ifndef __MGMT_CLIENT_UTIL_H__
#define __MGMT_CLIENT_UTIL_H__

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <netdb.h>

#define MAX_RESPONSE_LEN 512
#define EOL "\r\n"
#define EOM EOL "." EOL

typedef enum {
    CMD_CAPA = 0,
    CMD_TOKEN,
    CMD_STATS,
    CMD_USERS,
    CMD_BUFFSIZE,
    CMD_DISSECTOR_STATUS,
    CMD_SET_BUFFSIZE,
    CMD_SET_DISSECTOR_STATUS,
    CMD_ADD_USER,
    CMD_SIZE,
} MgmtCommands;

int tcpClientSocket(const char *host, const char *service);
bool read_hello(int sock);

bool authenticate(int sock, const char *token);
bool capa(int sock);
bool stats(int sock);
bool users(int sock);
bool buffsize(int sock);
bool dissector_status(int sock);
bool set_buffsize(int sock, size_t size);
bool set_dissector_status(int sock, const char *status);
bool add_user(int sock, const char *username_password);

#endif