#ifndef CLIENTS_H
#define CLIENTS_H

#include <buffer.h>
#include <stdint.h>
#include <netdb.h>
#include "selector.h"

#define MAX_SOCKETS 3
#define BUFFSIZE 2048

typedef struct client client;
struct client {
    // Sockets to handle communication between client and socket
    int client_sock, origin_sock;

    // buffers to store and send
    uint8_t client_buf_raw[BUFFSIZE], origin_buf_raw[BUFFSIZE];
    buffer client_buf, origin_buf;

    // handlers
    fd_handler client_handler, origin_handler;

    //
    struct addrinfo *resolution, *current;
    
    // Posición en el arreglo de clientes. Sirve para hacer clean-up
    size_t index;
};

// Maneja conexiones de nuevos clients
void master_read_handler(struct selector_key *key);

#endif