#ifndef CLIENTS_H
#define CLIENTS_H

#include "buffer.h"
#include <stdint.h>
#include <netdb.h>
#include "selector.h"
#include "stm.h"
#include <errno.h>
#include "pop3_sniffer.h"

#define MAX_CREDENTIALS 256
#define BUFFSIZE 2048
#define checkEOF(count) (count == 0 || (count < 0 && errno != EAGAIN))

extern fd_selector selector;

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
    uint8_t *client_buf_raw, *origin_buf_raw;
	buffer client_buf, origin_buf;

    char socks_user[MAX_CREDENTIALS];

	// status of socket ends
	enum socket_ends active_ends;
	
    // Nombres para hacer la resolucion
    char * dest_fqdn;
    int dest_port;

    // Resolución de nombres
    struct addrinfo *resolution, *curr_addr;

    // Máquina de estados
    struct state_machine *stm;

    pop3_parser *pop3_parser;
};

// Maneja conexiones de nuevos clients
void master_read_handler(struct selector_key *key);

/*
 * Avisa de la terminación de un extremo del pipeline
*/
unsigned closeClient(client *client, enum socket_ends level, struct selector_key *key);

enum socks5_states
{
	AUTH_METHOD = 0,
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
