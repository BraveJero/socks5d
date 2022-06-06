#ifndef CLIENTS_H
#define CLIENTS_H

#include <buffer.h>
#include <stdint.h>
#include "selector.h"

typedef struct client client;

struct client {
    fd_selector selector;
    // 0 for client, 1 for origin
    int socks[2];
    uint8_t client_buf_raw[2048], origin_buf_raw[2048];
    // 0 for client, 1 for origin
    buffer bufs[2];
};

void handle_master_read(void *data);

void handle_read(void *data);

void handle_write(void *data);

#endif