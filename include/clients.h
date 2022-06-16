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
 * Lee bytes de sd y los deja en el buffer
 * Retorna la cantidad de bytes que leyó y dejó en el buffer
*/
ssize_t read_from_sock(int sd, buffer *b);

/*
 * Lee bytes del buffer y los envía por sd
 * Retorna la cantidad de bytes que envió
*/
ssize_t write_to_sock(int sd, buffer *b);

/*
 * Avisa de la terminación de un extremo del pipeline
*/
unsigned closeClient(client *client, enum socket_ends level, fd_selector selector);

enum socks5_states
{
	AUTH_METHOD,
	REQUEST,
	RESOLVING,
	CONNECTING,
	PROXY,
	DONE,
	FAILED,
};

#define ATTACHMENT(x) ((client *) x->data)

#endif
