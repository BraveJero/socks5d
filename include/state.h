#ifndef _STATS_H_
#define _STATS_H_

#include <stdbool.h>
#include <stddef.h>

// Funciones para obtener y modificar el estado actual del servidor
size_t get_transferred_bytes(void);
size_t get_all_connections(void);
size_t get_current_clients(void);
size_t get_buffsize(void);
bool get_dissector_state(void);

void add_bytes(size_t bytes);
bool add_proxy_client(void);
bool add_mgmt_client(void);
void rm_proxy_client(void);
void rm_mgmt_client(void);
void set_buffsize(size_t size);
void set_dissector_state(bool state);

#endif
