#pragma once
#include "selector.h"
#include "buffer.h"
#include "mgmt_protocol.re.h"

#define MGMT_BUFFSIZE 512

typedef struct mgmt_client {
    int fd;

    uint8_t write_buf_raw[MGMT_BUFFSIZE]; 
    char read_buf_raw[MGMT_BUFFSIZE];
    struct buffer write_buf;

    int cond;

    Input input;
    bool quitted;

} mgmt_client;

void mgmt_master_read_handler(struct selector_key *key);
bool processMgmtClient(mgmt_client *c);

