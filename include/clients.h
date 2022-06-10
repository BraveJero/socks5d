#ifndef CLIENTS_H
#define CLIENTS_H

#include "buffer.h"
#include <stdint.h>
#include <netdb.h>
#include "selector.h"
#include "stm.h"

#define MAX_SOCKETS 3
#define BUFFSIZE 2048

typedef struct client client;
struct client {
    // Sockets to handle communication between client and socket
    int client_sock, origin_sock;

    // buffers to store and send
    uint8_t client_buf_raw[BUFFSIZE], origin_buf_raw[BUFFSIZE];
    buffer client_buf, origin_buf;

    // Nombres para hacer la resolucion
    char * dest_fqdn;
    int dest_port;

    // Resolución de nombres
    struct addrinfo *resolution, *curr_addr;

    // Máquina de estados
    struct state_machine *stm;
};

// Maneja conexiones de nuevos clients
void master_read_handler(struct selector_key *key);

enum socks5_states {
    RESOLVING,
    CONNECTING,
    PROXY,
    DONE,
    FAILED,
};


#endif