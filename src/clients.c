#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>

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
#define POP3_PORT 110

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
    if(!add_proxy_client())
    {
        logger(ERROR, "No more space for clients! Waiting...");
        return;
    }

    struct sockaddr_storage clntAddr;
    socklen_t clntAddrLen = sizeof(clntAddr);

    int new_socket = accept(key->fd, (struct sockaddr *) &clntAddr, &clntAddrLen);
    if(new_socket < 0){
        logger(DEBUG, "accept() failed. New connection refused");
        rm_proxy_client();
        return;
    }
            
    selector_fd_set_nio(new_socket);
    client *new_client = create_client(new_socket);

    if(new_client == NULL) {
        close(new_socket);
        rm_proxy_client();
        return;
    }

    memcpy(&(new_client)->client_addr, &clntAddr, clntAddrLen);
    selector_register(key->s, new_client->client_sock, &socks5_handler, OP_READ, new_client);

    return;
}

client *create_client(int sock) {
    client *new_client = malloc(sizeof(client));
    if(new_client == NULL)
        goto fail;

    *new_client = (struct client)
    {
        .client_sock = sock,
        .stm = malloc(sizeof(struct state_machine)),
        .origin_sock = -1,
        .active_ends = CLIENT,
    };
    if(new_client->stm == NULL)
        goto fail;
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
        free(new_client->stm);
        free(new_client->origin_buf_raw);
        free(new_client->client_buf_raw);
        goto fail;
    }

    new_client->socks_user[0] = '?';
    new_client->socks_user[1] = '\0';

    stm_init(new_client->stm);

    buffer_init(&(new_client->client_buf), buffsize, new_client->client_buf_raw);
    buffer_init(&(new_client->origin_buf), buffsize, new_client->origin_buf_raw);
    return new_client;

    fail:
    free(new_client);
    logger(DEBUG, "malloc() failed. New connection refused");
    return NULL;
}

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

static void start_name_resolution(unsigned state, struct selector_key *key)
{
    client *client = ATTACHMENT(key);

    pthread_t tid;
    pthread_create(&tid, NULL, thread_name_resolution, client);
}

static void *thread_name_resolution(void *data) {
    client *c = (client*)data;

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
    logger(DEBUG, "Calling getaddrinfo for %s:%s", c->dest_fqdn, port_buf);
    int err = getaddrinfo(c->dest_fqdn, port_buf, &hints, &(c->resolution));
    if(err != 0) {
        logger(DEBUG, "Error in getaddrinfo: %s", gai_strerror(err));
    }

    selector_notify_block(selector, c->client_sock);
    return NULL;
}

unsigned handle_finished_resolution(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    c->curr_addr = c->resolution;
    return CONNECTING;
}

void create_connection(const unsigned state, struct selector_key *key) {
    client *c = ATTACHMENT(key);
    // Truquito para que se llame a origin_check_connection
    selector_add_interest(key->s, c->client_sock, OP_WRITE);
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
    c->origin_sock = socket(c->curr_addr->ai_family, c->curr_addr->ai_socktype, c->curr_addr->ai_protocol);
    if (c->origin_sock >= 0) {
        selector_fd_set_nio(c->origin_sock);
        char adrr_buf[1024];
        logger(DEBUG, "%s", printAddressPort(c->curr_addr, adrr_buf));
        // Establish the connection to the server
        if ( connect(c->origin_sock, c->curr_addr->ai_addr, c->curr_addr->ai_addrlen) == 0 || errno == EINPROGRESS) {
            if(selector_register(key->s, c->origin_sock, &socks5_handler, OP_WRITE, c) != SELECTOR_SUCCESS)
            {
                close(c->origin_sock);
                logger(ERROR, "Can't register client socket");
                server_reply(c, REPLY_SERVER_FAILURE, ATYP_IPV4, EMPTY_IP, 0);
                return closeClient(c,  CLIENT_READ, key);
            }
            logger(DEBUG, "Connection to origin in progress");
            return CONNECTING;
        }
        enum server_reply_type reply = check_connection_error(errno);
        server_reply(c, reply, ATYP_IPV4, EMPTY_IP, 0);
        return closeClient(c,  CLIENT_READ, key);
    }
    logger(ERROR, "Can't create client socket");
    server_reply(c, REPLY_SERVER_FAILURE, ATYP_IPV4, EMPTY_IP, 0);
    return closeClient(c,  CLIENT_READ, key);
}

unsigned origin_check_connection(struct selector_key *key) {
    client *c = ATTACHMENT(key);

    if(key->fd == c->client_sock)
    {
        selector_remove_interest(key->s, c->client_sock, OP_WRITE);
        // Invalid FQDN
        if(c->curr_addr == NULL)
        {
            server_reply(c, REPLY_HOST_UNREACHABLE, ATYP_IPV4, EMPTY_IP, 0);
            return closeClient(c,  CLIENT_READ, key);
        }
        return try_connect(key);
    }

    int error;
    socklen_t len = sizeof(error);
    getsockopt(c->origin_sock, SOL_SOCKET, SO_ERROR, &error, &len);
    if(error == 0) {
        char addr_buf[64];
        printAddressPort(c->curr_addr, addr_buf);
		c->active_ends |= ORIGIN;
        selector_set_interest(key->s, c->origin_sock, OP_READ | OP_WRITE);
        selector_set_interest(key->s, c->client_sock, OP_READ | OP_WRITE);

        struct sockaddr_storage saddr;
        socklen_t slen = sizeof(saddr);
        getsockname(c->origin_sock, (struct sockaddr*)&saddr, &slen);
        if(c->curr_addr->ai_family == AF_INET)
        {
            struct sockaddr_in *sin = (void*)&saddr;
            server_reply(c, REPLY_SUCCEEDED, ATYP_IPV4, (uint8_t*)&sin->sin_addr, sin->sin_port);
        }
        else
        {
            struct sockaddr_in6 *sin6 = (void*)&saddr;
            server_reply(c, REPLY_SUCCEEDED, ATYP_IPV6, (uint8_t*)&sin6->sin6_addr, sin6->sin6_port);
        }

        uint16_t port = 0;
        if(c->curr_addr->ai_family == AF_INET) {
            struct sockaddr_in *sinp;
            sinp = (struct sockaddr_in *) c->curr_addr->ai_addr;
            port = ntohs(sinp->sin_port);
        } else if(c->curr_addr->ai_family == AF_INET) {
            struct sockaddr_in6 *sinp;
            sinp = (struct sockaddr_in6 *) c->curr_addr->ai_addr;
            port = ntohs(sinp->sin6_port);
        }

        if(port == POP3_PORT) {
            logger(DEBUG, "<%s> connected to a POP3 port. Sniffing...", c->socks_user);
            c->pop3_parser = malloc(sizeof(pop3_parser));
            if(c->pop3_parser != NULL) init_parser(c->pop3_parser);
        }
        return PROXY;
    } else {
        logger(ERROR, "Error: %d %s", error, strerror(error));
        selector_unregister_fd(key->s, c->origin_sock);
        if(c->curr_addr->ai_next != NULL)
        {
            c->curr_addr = c->curr_addr->ai_next;
            return try_connect(key);
        }

        // No hay más opciones. Responder con el error
        enum server_reply_type reply = check_connection_error(error);
        server_reply(c, reply, ATYP_IPV4, EMPTY_IP, 0);
        return closeClient(c,  CLIENT_READ, key);
    }
}

unsigned handle_proxy_read(struct selector_key *key) {
    ssize_t bytes_read = -1;
	client *c = ATTACHMENT(key);
	
    if(c->client_sock == key->fd) { // Leer del socket cliente y almacenar en el buffer del origen
        if(!buffer_can_write(&c->origin_buf))
        {
            selector_mask_interest(key->s, c->client_sock, OP_READ);
            goto next;
        }

        bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
		if (checkEOF(bytes_read))
			return closeClient(c,  CLIENT_READ, key);
        selector_unmask_interest(key->s, c->origin_sock, OP_WRITE);

        if(get_dissector_state() && c->pop3_parser != NULL) {
            if(pop3_parse(c->pop3_parser, &(c->origin_buf))) {
                log_sniffer_info(c, c->pop3_parser->user, c->pop3_parser->pass);
            }
        }
    } else if(c->origin_sock == key->fd) { // Leer en el socket del origen y guardar en el buffer del usuario
        if(!buffer_can_write(&c->client_buf))
        {
            selector_mask_interest(key->s, c->origin_sock, OP_READ);
            goto next;
        }
        
        bytes_read = read_from_sock(c->origin_sock, &(c->client_buf));
		if (checkEOF(bytes_read))
			return closeClient(c,  ORIGIN_READ, key);
        selector_unmask_interest(key->s, c->client_sock, OP_WRITE);

        uint8_t first = c->client_buf.read[0];
        if(c->pop3_parser == NULL && (first == '+' || first == '-')) {
            logger(DEBUG, "POP3-like response. Sniffing...");
            c->pop3_parser = malloc(sizeof(pop3_parser));
            if(c->pop3_parser != NULL) init_parser(c->pop3_parser);
        }
	}
    else
        abort();

    next:
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
                selector_mask_interest(key->s, key->fd, OP_WRITE);
                return stm_state(c->stm);
            }
        }
        buffer_read_ptr(&(c->client_buf), &bytes_to_write);
        bytes_left = write_to_sock(c->client_sock, &(c->client_buf));
        if (bytes_left != 0 && checkEOF(bytes_left))
            return closeClient(c,  CLIENT_WRITE, key);
        selector_unmask_interest(key->s, c->origin_sock, OP_READ);
    }
    else if (c->origin_sock == key->fd)
    { // Leer en el socket del origen y guardar en el buffer del usuario
        if (!buffer_can_read(&c->origin_buf))
        {
            if(!hasFlag(c->active_ends, CLIENT_READ))
                return closeClient(c,  ORIGIN_WRITE, key);
            else
            {
                selector_mask_interest(key->s, key->fd, OP_WRITE);
                return stm_state(c->stm);
            }
        }
        buffer_read_ptr(&(c->origin_buf), &bytes_to_write);
        bytes_left = write_to_sock(c->origin_sock, &(c->origin_buf));
        if (bytes_left != 0 && checkEOF(bytes_left))
            return closeClient(c,  ORIGIN_WRITE, key);
        selector_unmask_interest(key->s, c->client_sock, OP_READ);
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
    {
        selector_add_interest(key->s, c->client_sock, OP_WRITE);
        selector_unmask_interest(key->s, c->client_sock, OP_WRITE);
    }
    if(hasFlag(c->active_ends, ORIGIN_WRITE))
    {
        selector_add_interest(key->s, c->origin_sock, OP_WRITE);
        selector_unmask_interest(key->s, c->client_sock, OP_WRITE);
    }
}

static void client_destroy(unsigned state, struct selector_key *key) {
    client *c = ATTACHMENT(key);
	rm_proxy_client();
    logger(DEBUG, "Cleaning client");
    if(c->resolution != NULL)
    {
        freeaddrinfo(c->resolution);
    }
    else if(c->curr_addr != NULL)
    {
        free(c->curr_addr->ai_addr);
        free(c->curr_addr);
    }
    free(c->dest_fqdn);
    free(c->pop3_parser);
    free(c->stm);
    free(c->origin_buf_raw);
    free(c->client_buf_raw);
    free(c);
}
