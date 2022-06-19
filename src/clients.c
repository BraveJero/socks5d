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
#include "socketsIO.h"
#include "stm.h"
#include "util.h"
#include "tcpClientUtil.h"
#include "state.h"
#include "util.h"
#include "tcpServerUtil.h"
#include "handshake.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))
#define hasFlag(x, f) (bool)((x) & (f))

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

void enable_write(unsigned state, struct selector_key *key);

static void start_name_resolution(unsigned state, struct selector_key *key);

// Definicion de las acciones de cada estado
static const struct state_definition state_actions[] = {
    {
        .state = AUTH_METHOD,
        .on_read_ready = read_auth_method,
    },
    {
        .state = PLAIN_AUTH,
        .on_read_ready = read_plain_auth,
        .on_write_ready = handle_proxy_write
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
        .on_write_ready = handle_proxy_write,
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
        .on_arrival = enable_write,
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

unsigned closeClient(client *client, enum socket_ends level, struct selector_key *key)
{
	level &= client->active_ends;
	if (hasFlag(level, CLIENT_READ))
	{
        selector_remove_interest(key->s, client->client_sock, OP_READ);
		shutdown(client->client_sock, SHUT_RD);
	}
	if (hasFlag(level, ORIGIN_WRITE))
	{
        selector_remove_interest(key->s, client->origin_sock, OP_WRITE);
		shutdown(client->origin_sock, SHUT_WR);
	}
	if (hasFlag(level, CLIENT_WRITE))
	{
        selector_remove_interest(key->s, client->client_sock, OP_WRITE);
		shutdown(client->client_sock, SHUT_WR);
	}
	if (hasFlag(level, ORIGIN_READ))
	{
        selector_remove_interest(key->s, client->origin_sock, OP_READ);
		shutdown(client->origin_sock, SHUT_RD);
	}
	client->active_ends &= ~level;

	// Client-server is closed both ways
	if (hasFlag(level, CLIENT) && !hasFlag(client->active_ends, CLIENT))
	{
		selector_unregister_fd(key->s, client->client_sock);
		if (client->client_sock > 0)
			close(client->client_sock);
	}
	if (hasFlag(level, ORIGIN) && !hasFlag(client->active_ends, ORIGIN))
	{
		selector_unregister_fd(key->s, client->origin_sock);
		if (client->origin_sock > 0)
			close(client->origin_sock);
	}

	if (hasFlag(level, CLIENT_WRITE))
	{
		return closeClient(client, CLIENT_READ | ORIGIN, key);
	}

	if (hasFlag(level, ORIGIN_WRITE))
	{
		return closeClient(client, CLIENT_READ, key);
	}

	// Free client structures
    if (!client->active_ends)
        return DONE;
    if(!hasFlag(client->active_ends, CLIENT_READ | ORIGIN_READ))
        return WRITE_REMAINING;
    enable_write(stm_state(client->stm), key);
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

    selector_register(key->s, new_client->client_sock, &socks5_handler, OP_READ, new_client);

    return;
}

client *create_client(int sock) {
    client *new_client = malloc(sizeof(client));
    if(new_client == NULL) {
        logger(DEBUG, "malloc() failed. New connection refused");
        close(sock);
    }

    *new_client = (struct client)
    {
        .client_sock = sock,
        .stm = malloc(sizeof(struct state_machine)),
        .origin_sock = -1,
        .active_ends = CLIENT,
    };
    *new_client->stm = (struct state_machine)
    {
        .initial = AUTH_METHOD,
        .states = state_actions,
        .max_state = WRITE_REMAINING,
    };

    size_t buffsize = get_buffsize();

    new_client->client_buf_raw = malloc(sizeof(uint8_t) * buffsize);
    new_client->origin_buf_raw = malloc(sizeof(uint8_t) * buffsize);

    if(new_client->client_buf_raw == NULL || new_client->origin_buf_raw == NULL) {
        free(new_client);
        logger(DEBUG, "malloc() failed: %s", strerror(errno));
        return NULL;
    }

    new_client->socks_user[0] = '\0';

    stm_init(new_client->stm);

    buffer_init(&(new_client->client_buf), buffsize, new_client->client_buf_raw);
    buffer_init(&(new_client->origin_buf), buffsize, new_client->origin_buf_raw);
    return new_client;
}

unsigned handle_finished_resolution(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    assert(c->resolution != NULL);
    c->curr_addr = c->resolution;
    return CONNECTING;
}

// static void socksv5_done(struct selector_key *key)
// {
// 	closeClient(ATTACHMENT(key),  CLIENT | ORIGIN, key);
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

    int err = getaddrinfo(c->dest_fqdn, port_buf, &hints, &(c->resolution));
    if(err != 0) {
        logger(ERROR, gai_strerror(err));
    }

    selector_notify_block(key->s, key->fd);
    free(data);
    return NULL;
}

void create_connection(const unsigned state, struct selector_key *key) {
    client *c = ATTACHMENT(key);
    c->origin_sock = socket(c->curr_addr->ai_family, c->curr_addr->ai_socktype, c->curr_addr->ai_protocol);
    if(c->origin_sock < 0) {
        logger(ERROR, "Error in socket()");
        perror("socket()");
    }
    selector_fd_set_nio(c->origin_sock);
    // selector_fd_set_nio(c->client_sock);
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
        if ( connect(sock, c->curr_addr->ai_addr, c->curr_addr->ai_addrlen) == 0 || errno == EINPROGRESS) {
            selector_register(key->s, c->origin_sock, &socks5_handler, OP_WRITE, c);
            // logger(ERROR, selector_error(ss));
            
            logger(INFO, "Connection to origin in progress");
            return CONNECTING;
        }
        enum server_reply_type reply = check_connection_error(errno);
        server_reply(&c->client_buf, reply, ATYP_IPV4, EMPTY_IP, 0);
        return closeClient(c,  CLIENT_READ, key);
    } else {
        logger(DEBUG, "Can't create client socket");
        server_reply(&c->client_buf, REPLY_SERVER_FAILURE, ATYP_IPV4, EMPTY_IP, 0);
        return closeClient(c,  CLIENT_READ, key);
    }
}

unsigned origin_check_connection(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(c->origin_sock, SOL_SOCKET, SO_ERROR, &error, &len);
    if(error == 0) {
        char addr_buf[64];
        printAddressPort(c->curr_addr, addr_buf);
        logger(INFO, "<%s> is connected to %s", c->socks_user, addr_buf);
		c->active_ends |= ORIGIN;
        selector_set_interest(key->s, c->origin_sock, OP_READ | OP_WRITE);
        selector_set_interest(key->s, c->client_sock, OP_READ | OP_WRITE);
        server_reply(&c->client_buf, REPLY_SUCCEEDED, ATYP_IPV4, EMPTY_IP, 0);
        add_connection();
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
        return closeClient(c,  CLIENT_READ, key);
    }
}

unsigned handle_proxy_read(struct selector_key *key) {
    ssize_t bytes_read = -1;
	client *c = ATTACHMENT(key);
	
    if(c->client_sock == key->fd) { // Leer del socket cliente y almacenar en el buffer del origen
        bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
		if (checkEOF(bytes_read))
			return closeClient(c,  CLIENT_READ, key);
        selector_add_interest(key->s, c->origin_sock, OP_WRITE);
    } else if(c->origin_sock == key->fd) { // Leer en el socket del origen y guardar en el buffer del usuario
        bytes_read = read_from_sock(c->origin_sock, &(c->client_buf));
		if (checkEOF(bytes_read))
			return closeClient(c,  ORIGIN_READ, key);
        selector_add_interest(key->s, c->client_sock, OP_WRITE);
	}
    else
        abort();
	
    return stm_state(c->stm);
}

unsigned handle_proxy_write(struct selector_key *key) {
    ssize_t bytes_left = 0;
    size_t bytes_to_write = 0;
	client *c = ATTACHMENT(key);

    if (c->client_sock == key->fd)
    { // Envio al socket lo que haya en el buffer
        if (!buffer_can_read(&c->client_buf))
        {
            if(!hasFlag(c->active_ends, ORIGIN_READ) && stm_state(c->stm) >= PROXY)
                return closeClient(c,  CLIENT_WRITE, key);
            else
            {
                selector_remove_interest(key->s, key->fd, OP_WRITE);
                return stm_state(c->stm);
            }
        }
        buffer_read_ptr(&(c->client_buf), &bytes_to_write);
        bytes_left = write_to_sock(c->client_sock, &(c->client_buf));
        if (bytes_left != 0 && checkEOF(bytes_left))
            return closeClient(c,  CLIENT_WRITE, key);
    }
    else if (c->origin_sock == key->fd)
    { // Leer en el socket del origen y guardar en el buffer del usuario
        if (!buffer_can_read(&c->origin_buf))
        {
            if(!hasFlag(c->active_ends, CLIENT_READ))
                return closeClient(c,  ORIGIN_WRITE, key);
            else
            {
                selector_remove_interest(key->s, key->fd, OP_WRITE);
                return stm_state(c->stm);
            }
        }
        buffer_read_ptr(&(c->origin_buf), &bytes_to_write);
        bytes_left = write_to_sock(c->origin_sock, &(c->origin_buf));
        if (bytes_left != 0 && checkEOF(bytes_left))
            return closeClient(c,  ORIGIN_WRITE, key);
    }
    else
        abort();

    if(stm_state(c->stm) == PROXY) {
        add_bytes(bytes_to_write - bytes_left);
    }

    return stm_state(c->stm);
}

void enable_write(unsigned state, struct selector_key *key)
{
	client *c = ATTACHMENT(key);
    if(hasFlag(c->active_ends, CLIENT_WRITE))
        selector_add_interest(key->s, c->client_sock, OP_WRITE);
    if(hasFlag(c->active_ends, ORIGIN_WRITE))
        selector_add_interest(key->s, c->origin_sock, OP_WRITE);
}

static void client_destroy(unsigned state, struct selector_key *key) {
    client *c = ATTACHMENT(key);
	rm_connection();
    if(c->resolution != NULL)
    {
        logger(DEBUG, "Cleaning client");
        freeaddrinfo(c->resolution);
    }
    else if(c->curr_addr != NULL)
    {
        free(c->curr_addr->ai_addr);
        free(c->curr_addr);
    }
    free(c->dest_fqdn);
    free(c->stm);
    free(c->origin_buf_raw);
    free(c->client_buf_raw);
    free(c);
}
