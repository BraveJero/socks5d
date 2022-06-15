#include "state.h"

static size_t current_connections;
static size_t all_connections;
static size_t transferred_bytes;
static size_t buffsize = 2048;
static bool dissector_state = true;


size_t get_transferred_bytes(void) {
    return transferred_bytes;
}

size_t get_all_connections(void) {
    return all_connections;
}

size_t get_current_connections(void) {
    return current_connections;
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

void add_connection(void) {
    current_connections++;
    all_connections++;
}

void rm_connection(void) {
    current_connections--;
}

void set_buffsize(size_t size) {
    if(size) buffsize = size;
}

void set_dissector_state(bool state) {
    dissector_state = state;
}
