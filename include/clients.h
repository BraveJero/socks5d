#ifndef CLIENTS_H
#define CLIENTS_H

#include "buffer.h"
#include <stdint.h>
#include <netdb.h>
#include "selector.h"
#include "stm.h"
#include <errno.h>

#define MAX_SOCKETS 3
#define BUFFSIZE 2048
#define checkEOF(count) (count == 0 || (count < 0 && errno != EAGAIN))

enum socket_ends
{
	CLIENT_WRITE = 1,
	CLIENT_READ = 2,
	CLIENT = CLIENT_READ | CLIENT_WRITE,
	ORIGIN_WRITE = 4,
	ORIGIN_READ = 8,
	ORIGIN = ORIGIN_READ | ORIGIN_WRITE,
};

typedef struct client client;
struct client {
    // Sockets to handle communication between client and socket
    int client_sock, origin_sock;

    // buffers to store and send
    uint8_t client_buf_raw[BUFFSIZE], origin_buf_raw[BUFFSIZE];
	buffer client_buf, origin_buf;

	// status of socket ends
	enum socket_ends active_ends;
	
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

/*
 * Avisa de la terminación de un extremo del pipeline
*/
unsigned closeClient(client *client, enum socket_ends level, struct selector_key *key);

enum socks5_states
{
	AUTH_METHOD,
	PLAIN_AUTH,
	REQUEST,
	RESOLVING,
	CONNECTING,
	PROXY,
	DONE,
	WRITE_REMAINING,
};

#define ATTACHMENT(x) ((struct client*) x->data)

#endif
