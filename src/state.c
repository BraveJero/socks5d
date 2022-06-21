#include "state.h"
#include "clients.h"

static size_t all_connections;
static size_t transferred_bytes;
static size_t buffsize = 2048;
static bool dissector_state = true;
static unsigned proxy_client_count = 0;
static unsigned mgmt_client_count = 0;

size_t get_transferred_bytes(void) {
    return transferred_bytes;
}

size_t get_all_connections(void) {
    return all_connections;
}

size_t get_current_clients(void) {
    return proxy_client_count;
}

size_t get_buffsize(void) {
    return buffsize;
}

bool get_dissector_state(void) {
    return dissector_state;
}

void add_bytes(size_t bytes) {
    transferred_bytes += bytes;
}

bool add_proxy_client(void) {
    if(mgmt_client_count + proxy_client_count * 2 <= 1020 - 2)
    {
        proxy_client_count++;
        return true;
    }
    return false;
}

bool add_mgmt_client(void) {
    if(mgmt_client_count + proxy_client_count * 2 <= 1020 - 1)
    {
        mgmt_client_count++;
        return true;
    }
    return false;
}

void rm_proxy_client(void) {
    proxy_client_count--;
}

void rm_mgmt_client(void) {
    mgmt_client_count--;
}

void set_buffsize(size_t size) {
    if(size) buffsize = size;
}

void set_dissector_state(bool state) {
    dissector_state = state;
}
