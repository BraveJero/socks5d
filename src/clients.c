#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>

#include "clients.h"
#include "logger.h"
#include "buffer.h"
#include "selector.h"
#include "stm.h"
#include "tcpClientUtil.h"
#include "util.h"
#include "tcpServerUtil.h"
#include "handshake.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))
#define hasFlag(x, f) (bool)((x) & (f))

static uint8_t buf[BUFFSIZE];

// Crea el cliente con el correspondiente sock
client *create_client(int socks);

// Cierra todos los sockets y libera la memoria.
static void client_destroy(unsigned state, struct selector_key *key);

// Hace el connect no bloqueante a la proxima direccion
unsigned try_connect(struct selector_key *key);

// Verifica si la conexion no bloqueante se establecio correctamente
unsigned origin_check_connection(struct selector_key *key);

// Crea el socket
void create_connection(const unsigned state, struct selector_key *key);

// Se le avisa que la resolucion de nombres fallo
unsigned handle_finished_resolution(struct selector_key *key);

// Resuelve nombres de forma asincronica
static void *thread_name_resolution(void *data);

unsigned handle_proxy_read(struct selector_key *key);

unsigned handle_proxy_write(struct selector_key *key);

static void start_name_resolution(unsigned state, struct selector_key *key);

// Definicion de las acciones de cada estado
static const struct state_definition state_actions[] = {
    {
        .state = AUTH_METHOD,
        .on_read_ready = read_auth_method,
        .on_write_ready = handle_proxy_write,
    },
    {
        .state = REQUEST,
        .on_read_ready = read_proxy_request,
        .on_write_ready = handle_proxy_write
    },
    {
        .state = RESOLVING,
        .on_arrival = start_name_resolution,
        .on_departure = NULL,
        .on_read_ready = NULL,
        .on_write_ready = NULL,
        .on_block_ready = handle_finished_resolution,
    }, {
        .state = CONNECTING,
        .on_arrival = create_connection,
        .on_departure = NULL,
        .on_read_ready = NULL,
        .on_write_ready = origin_check_connection,
    }, {
        .state = PROXY,
        .on_arrival = NULL,
        .on_departure = NULL,
        .on_read_ready = handle_proxy_read,
        .on_write_ready = handle_proxy_write,
    }, {
        .state = DONE,
        .on_arrival = client_destroy,
        .on_departure = NULL,
        .on_read_ready = NULL,
        .on_write_ready = NULL,
    }, {
        .state = WRITE_REMAINING,
        .on_write_ready = handle_proxy_write,
    }
};

///////////////////////////////////////////////////////////

//:)
static void socksv5_read   (struct selector_key *key);
static void socksv5_write  (struct selector_key *key);
static void socksv5_block  (struct selector_key *key);
static const struct fd_handler socks5_handler = {
    .handle_read   = socksv5_read,
    .handle_write  = socksv5_write,
    .handle_block  = socksv5_block,
};

static void start_name_resolution(unsigned state, struct selector_key *key)
{
    client *client = ATTACHMENT(key);
    struct selector_key *dup_key = malloc(sizeof(*key));
    
    dup_key->data = client;
    dup_key->fd = client->client_sock;
    dup_key->s = key->s;

    pthread_t tid;
    pthread_create(&tid, NULL, thread_name_resolution, dup_key);
}

unsigned closeClient(client *client, enum socket_ends level, fd_selector selector)
{
	level &= client->active_ends;
	if (hasFlag(level, CLIENT_READ))
	{
        selector_remove_interest(selector, client->client_sock, OP_READ);
		shutdown(client->client_sock, SHUT_RD);
	}
	if (hasFlag(level, ORIGIN_WRITE))
	{
        selector_remove_interest(selector, client->origin_sock, OP_WRITE);
		shutdown(client->origin_sock, SHUT_WR);
	}
	if (hasFlag(level, CLIENT_WRITE))
	{
        selector_remove_interest(selector, client->client_sock, OP_WRITE);
		shutdown(client->client_sock, SHUT_WR);
	}
	if (hasFlag(level, ORIGIN_READ))
	{
        selector_remove_interest(selector, client->origin_sock, OP_READ);
		shutdown(client->origin_sock, SHUT_RD);
	}
	client->active_ends &= ~level;

	// Client-server is closed both ways
	if (hasFlag(level, CLIENT) && !hasFlag(client->active_ends, CLIENT))
	{
		selector_unregister_fd(selector, client->client_sock);
		if (client->client_sock > 0)
			close(client->client_sock);
	}
	if (hasFlag(level, ORIGIN) && !hasFlag(client->active_ends, ORIGIN))
	{
		selector_unregister_fd(selector, client->origin_sock);
		if (client->origin_sock > 0)
			close(client->origin_sock);
	}

	if (hasFlag(level, CLIENT_WRITE))
	{
		return closeClient(client, CLIENT_READ | ORIGIN, selector);
	}

	if (hasFlag(level, ORIGIN_WRITE))
	{
		return closeClient(client, ORIGIN_READ, selector);
	}

	// Free client structures
	if (!client->active_ends)
		return DONE;
	return stm_state(client->stm);
}

// If master has activity, it must be due to incoming connections
void master_read_handler(struct selector_key *key) {
    int new_socket = acceptTCPConnection(key->fd);
    if(new_socket < 0){
        logger(DEBUG, "accept() failed. New connection refused");
        return;
    }
            
    selector_fd_set_nio(new_socket);
    client *new_client = create_client(new_socket);

    if(new_client == NULL) {
        // catch error
    }

    selector_register(key->s, new_client->client_sock, &socks5_handler, OP_NOOP, new_client);

    return;
}

client *create_client(int sock) {
    client *new_client = malloc(sizeof(client));
    if(new_client == NULL) {
        logger(DEBUG, "malloc() failed. New connection refused");
        close(sock);
    }
    new_client->client_sock = sock;
    new_client->origin_sock = -1;

    new_client->stm = malloc(sizeof(struct state_machine));
    new_client->stm->states = state_actions;
    new_client->stm->initial = RESOLVING;
    new_client->stm->max_state = WRITE_REMAINING;

    new_client->dest_fqdn = "localhost";
	new_client->dest_port = 61441;

	new_client->active_ends = CLIENT;

    stm_init(new_client->stm);

    buffer_init(&(new_client->client_buf), BUFFSIZE, new_client->client_buf_raw);
    buffer_init(&(new_client->origin_buf), BUFFSIZE, new_client->origin_buf_raw);
    return new_client;
}

unsigned handle_finished_resolution(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    assert(c->resolution != NULL);
    return CONNECTING;
}

// static void socksv5_done(struct selector_key *key)
// {
// 	closeClient(ATTACHMENT(key), CLIENT | ORIGIN, key->s);
// }

static void
socksv5_read(struct selector_key *key) {
    struct state_machine *stm   = ATTACHMENT(key)->stm;
    stm_handler_read(stm, key);
}

static void
socksv5_write(struct selector_key *key) {
    struct state_machine *stm   = ATTACHMENT(key)->stm;
    stm_handler_write(stm, key);
}

static void
socksv5_block(struct selector_key *key) {
    struct state_machine *stm   = ATTACHMENT(key)->stm;
    stm_handler_block(stm, key);
}

static void *thread_name_resolution(void *data) {
    struct selector_key *key = (struct selector_key*) data;
    client *c = ATTACHMENT(key);

    pthread_detach(pthread_self());
    c->resolution = NULL;
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
        .ai_protocol = 0,
        .ai_canonname = NULL,
        .ai_addr = NULL,
        .ai_next = NULL,
    };
    
    char port_buf[7];
    snprintf(port_buf, sizeof(port_buf), "%d", ntohs(c->dest_port));

    logger(INFO, "ntohs(): %s", port_buf);

    int err = getaddrinfo(c->dest_fqdn, "61441", &hints, &(c->resolution));
    if(err != 0) {
        logger(ERROR, gai_strerror(err));
    }

    selector_notify_block(key->s, key->fd);
    free(data);
    return NULL;
}

void create_connection(const unsigned state, struct selector_key *key) {
    client *c = ATTACHMENT(key);
    c->curr_addr = c->resolution;
    c->origin_sock = socket(c->curr_addr->ai_family, c->curr_addr->ai_socktype, c->curr_addr->ai_protocol);
    if(c->origin_sock < 0) {
        logger(ERROR, "Error in socket()");
        perror("socket()");
    }
    selector_fd_set_nio(c->origin_sock);
    selector_fd_set_nio(c->client_sock);
    try_connect(key);
}

enum server_reply_type check_connection_error(int error)
{
    switch (error)
    {
        case EADDRNOTAVAIL:
            return REPLY_ADDRESS_NOT_SUPPORTED;
        case ECONNRESET:
            return REPLY_NOT_ALLOWED;
        case ECONNREFUSED: 
            return REPLY_CONNECTION_REFUSED;
        case ENETUNREACH:
            return REPLY_NETWORK_UNREACHABLE;
        case EHOSTUNREACH:
            return REPLY_HOST_UNREACHABLE;
        case ETIMEDOUT:
            return REPLY_TTL_EXPIRED;
        default:
            return REPLY_SERVER_FAILURE;
    }
}

unsigned try_connect(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    assert(c->curr_addr != NULL);
    int sock = c->origin_sock;
    if (sock >= 0) {
        errno = 0;
        char adrr_buf[1024];
        logger(DEBUG, "%s", printAddressPort(c->curr_addr, adrr_buf));
        // Establish the connection to the server
        if ( connect(sock, c->curr_addr->ai_addr, c->curr_addr->ai_addrlen) != 0) {
            if(errno == EINPROGRESS) {
                c->origin_sock = sock;
                selector_register(key->s, c->origin_sock, &socks5_handler, OP_WRITE, c);
                // logger(ERROR, selector_error(ss));
                
                logger(INFO, "Connection to origin in progress");
                return CONNECTING;
            }
        }
        enum server_reply_type reply = check_connection_error(errno);
        server_reply(&c->client_buf, reply, ATYP_IPV4, EMPTY_IP, 0);
        return closeClient(c, CLIENT, key->s);
    } else {
        logger(DEBUG, "Can't create client socket");
        server_reply(&c->client_buf, REPLY_SERVER_FAILURE, ATYP_IPV4, EMPTY_IP, 0);
        return closeClient(c, CLIENT, key->s);
    }
}

unsigned origin_check_connection(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(c->origin_sock, SOL_SOCKET, SO_ERROR, &error, &len);
    if(error == 0) {
        logger(INFO, "Connection to origin succesful");

		c->active_ends |= ORIGIN;
        selector_set_interest(key->s, c->origin_sock, OP_READ | OP_WRITE);
        selector_set_interest(key->s, c->client_sock, OP_READ | OP_WRITE);
        return PROXY;
    } else {
        logger(ERROR, "Error: %d %s", error, strerror(error));
        c->curr_addr = c->curr_addr->ai_next;
        selector_remove_interest(key->s, c->origin_sock, OP_READ | OP_WRITE);
        if(c->curr_addr != NULL)
            return try_connect(key);

        // No hay más opciones. Responder con el error
        enum server_reply_type reply = check_connection_error(error);
        server_reply(&c->client_buf, reply, ATYP_IPV4, EMPTY_IP, 0);
        return closeClient(c, CLIENT, key->s);
    }
}

unsigned handle_proxy_read(struct selector_key *key) {
    ssize_t bytes_read = -1;
	client *c = ATTACHMENT(key);
	
    if(c->client_sock == key->fd) { // Leer del socket cliente y almacenar en el buffer del origen
        bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
		if (checkEOF(bytes_read))
			return closeClient(c, CLIENT_READ, key->s);
    } else if(c->origin_sock == key->fd) { // Leer en el socket del origen y guardar en el buffer del usuario
        bytes_read = read_from_sock(c->origin_sock, &(c->client_buf));
		if (checkEOF(bytes_read))
			return closeClient(c, ORIGIN_READ, key->s);
	}
	
    return stm_state(c->stm);
}

unsigned handle_proxy_write(struct selector_key *key) {
    ssize_t bytes_left = -1;
	client *c = ATTACHMENT(key);

    if (c->client_sock == key->fd)
    { // Envio al socket lo que haya en el buffer
        if (!buffer_can_read(&c->client_buf))
            goto skip;
        
        bytes_left = write_to_sock(c->client_sock, &(c->client_buf));
        if (bytes_left != 0 && checkEOF(bytes_left))
            return closeClient(c, CLIENT_WRITE, key->s);
        // if(bytes_left == 0){
        //     if(selector_set_interest(key->s, c->client_sock, OP_READ) != SELECTOR_SUCCESS) {
        //         return FAILED;
        //     }
        // }
    }
    else if (c->origin_sock == key->fd)
    { // Leer en el socket del origen y guardar en el buffer del usuario
        if (!buffer_can_read(&c->origin_buf))
            goto skip;
        bytes_left = write_to_sock(c->origin_sock, &(c->origin_buf));
        if (bytes_left != 0 && checkEOF(bytes_left))
            return closeClient(c, ORIGIN_WRITE, key->s);
        // if(bytes_left == 0){
        //     if(selector_set_interest(key->s, c->origin_sock, OP_READ) != SELECTOR_SUCCESS) {
        //         return FAILED;
        //     }
        // }
    }
    else
        abort();

    skip:
    return stm_state(c->stm);
}

ssize_t read_from_sock(int sd, buffer *b) {
    size_t size;
    uint8_t *write_ptr = buffer_write_ptr(b, &size);
    size = (size > BUFFSIZE? BUFFSIZE : size);
    ssize_t bytes_rcv = read(sd, buf, size);
    if(bytes_rcv > 0) {
        memcpy(write_ptr, buf, bytes_rcv);
        buffer_write_adv(b, bytes_rcv);
    }
    logger(DEBUG, "Read %ld bytes from socket %d", bytes_rcv, sd);
    return bytes_rcv;
}


ssize_t write_to_sock(int sd, buffer *b) {
    size_t size;
    uint8_t *read_ptr = buffer_read_ptr(b, &size);
    ssize_t bytes_sent = send(sd, read_ptr, size, MSG_DONTWAIT);
    if(bytes_sent < 0) return bytes_sent;
    buffer_read_adv(b, bytes_sent);
    logger(DEBUG, "Sent %ld bytes through socket %d", bytes_sent, sd);
    return size - bytes_sent;
}

static void client_destroy(unsigned state, struct selector_key *key) {
	/*
     * TODO: problemita, si tenes mas de un sock para este cliente (o sea el par 
     * {cliente, origen} esta seteado en algo que no es {-1, -1}) se llama por duplicado
     * al unregister, que llama al socksv5_close que llama a la funcion que hacer frees 
     * y el sanitizer tira error porque esa memoria ya fue liberada.
    */
    client *c = ATTACHMENT(key);
	
    freeaddrinfo(c->resolution);
    free(c->stm);
    free(c);
}
