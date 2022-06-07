#ifndef CLIENTS_H
#define CLIENTS_H

#include <buffer.h>
#include <stdint.h>
#include "selector.h"

#define MAX_SOCKETS 3
#define BUFFSIZE 2048

typedef struct client client;
struct client {
    int client_sock, origin_sock;
    uint8_t client_buf_raw[BUFFSIZE], origin_buf_raw[BUFFSIZE];
    buffer client_buf, origin_buf;
    
    // Posición en el arreglo de clientes. Sirve para hacer clean-up
    size_t index;
};

// Maneja conexiones de nuevos clients
void master_read_handler(struct selector_key *key);

// Lee lo que el cliente ha enviado
void client_read_handler(struct selector_key *key);

// Envía al cliente lo que el origen envío
void client_write_handler(struct selector_key *key);

// Lee lo que el origen ha enviado
void origin_read_handler(struct selector_key *key);

// Envía al origen lo que el cliente ha enviado
void origin_write_handler(struct selector_key *key);


#endif