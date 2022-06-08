#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "clients.h"
#include "logger.h"
#include "buffer.h"
#include "tcpClientUtil.h"
#include "util.h"
#include "tcpServerUtil.h"

#define ATTACHMENT(x) ((client *) x->data)

static client *clients[MAX_SOCKETS];
static uint8_t buf[BUFFSIZE];

// Lee lo que el cliente ha enviado
void client_read_handler(struct selector_key *key);

// Envía al cliente lo que el origen envío
void client_write_handler(struct selector_key *key);

// Lee lo que el origen ha enviado
void origin_read_handler(struct selector_key *key);

// Envía al origen lo que el cliente ha enviado
void origin_write_handler(struct selector_key *key);

/*
 * Lee bytes de sd y los deja en el buffer
 * Retorna la cantidad de bytes que leyó y dejó en el buffer
*/
static ssize_t proxy_read(int sd, buffer *b);

/*
 * Lee bytes del buffer y los envía por sd
 * Retorna la cantidad de bytes que envió
*/
static ssize_t proxy_write(int sd, buffer *b);

// Cierra todos los sockets y libera la memoria.
static void close_client(client *c, fd_selector s);

void try_connect(client *c, fd_selector s);

void origin_check_connection(struct selector_key *key);

// If master has activity, it must be due to incoming connections
void master_read_handler(struct selector_key *key) {
    int new_socket = acceptTCPConnection(key->fd);
    if(new_socket < 0){
        logger(DEBUG, "accept() failed. New connection refused");
        return;
    }
    for(int i = 0; i < MAX_SOCKETS; i++) {
        if(clients[i] == NULL) {
            client *new_client = malloc(sizeof(client));
            if(new_client == NULL) {
                logger(DEBUG, "malloc() failed. New connection refused");
                close(new_socket);
            }
            new_client->client_sock = new_socket;
            new_client->current = new_client->resolution = tcpClientSocket("127.0.0.1", "9999");            

            new_client->client_handler.handle_read = client_read_handler;
            new_client->client_handler.handle_write = client_write_handler;
            new_client->client_handler.handle_block = NULL;
            new_client->client_handler.handle_close = NULL;

            new_client->origin_handler.handle_read = NULL;
            new_client->origin_handler.handle_write = origin_check_connection;
            new_client->origin_handler.handle_block = NULL;
            new_client->origin_handler.handle_close = NULL;

            buffer_init(&(new_client->client_buf), BUFFSIZE, new_client->client_buf_raw);
            buffer_init(&(new_client->origin_buf), BUFFSIZE, new_client->origin_buf_raw);

            new_client->index = i;
            clients[i] = new_client;

            try_connect(new_client, key->s);

            logger(INFO, "New connection, on socks {%d, %d}", new_socket, new_client->origin_sock);
            return;
        }
    }
}

void try_connect(client *c, fd_selector s) {
    if(c->current == NULL) return;
    int sock = socket(c->current->ai_family, c->current->ai_socktype, c->current->ai_protocol);
    if (sock >= 0) {
        errno = 0;
        selector_fd_set_nio(sock);
        // Establish the connection to the server
        if ( connect(sock, c->current->ai_addr, c->current->ai_addrlen) != 0) {
            if(errno == EINPROGRESS) {
                c->origin_sock = sock;
                selector_status ss = selector_register(s, c->origin_sock, &(c->origin_handler), OP_WRITE, c);
                logger(ERROR, selector_error(ss));
                
                logger(INFO, "Connection to origin in progress");
            }
        }
    } else {
        logger(DEBUG, "Can't create client socket"); 
    }
}

void origin_check_connection(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    int error;
    socklen_t len = sizeof(error);
    getsockopt(c->origin_sock, SOL_SOCKET, SO_ERROR, &error, &len);
    if(error == 0) {
        logger(INFO, "Connection to origin succesful");

        c->origin_handler.handle_read = origin_read_handler;
        c->origin_handler.handle_write = origin_write_handler;

        selector_set_interest(key->s, c->origin_sock, OP_READ);
        selector_register(key->s, c->client_sock, &(c->client_handler), OP_READ, c);
    } else {
        c->current = c->current->ai_next;
        close(c->origin_sock);
        selector_set_interest(key->s, c->origin_sock, OP_NOOP);
        try_connect(c, key->s);
    }
}

void client_read_handler(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    ssize_t bytes_rcv = proxy_read(c->client_sock, &(c->origin_buf));
    if(bytes_rcv <= 0) {
        // TODO: catch errors
        close_client(c, key->s);
        return;
    }
    if(selector_set_interest(key->s, c->origin_sock, OP_READ | OP_WRITE) != SELECTOR_SUCCESS) {
        logger(ERROR, "Error setting socket for write interest");
    }
}

void client_write_handler(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    ssize_t bytes_left = proxy_write(c->client_sock, &(c->client_buf));
    if(bytes_left < 0) {
        // TODO: catch errors
        close_client(c, key->s);
    }
    // If there are no bytes left to send, I just have to wait for read
    if(bytes_left == 0) {
        if(selector_set_interest(key->s, c->client_sock, OP_READ) != SELECTOR_SUCCESS) {
            logger(ERROR, "Error setting socket for read interest");
        }
    }
}

void origin_read_handler(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    ssize_t bytes_rcv = proxy_read(c->origin_sock, &(c->client_buf));
    if(bytes_rcv <= 0) {
        // TODO: catch errors
        close_client(c, key->s);
        return;
    }
    if(selector_set_interest(key->s, c->client_sock, OP_READ | OP_WRITE) != SELECTOR_SUCCESS) {
        logger(ERROR, "Error setting socket for write interest");
    }
}

void origin_write_handler(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    ssize_t bytes_left = proxy_write(c->origin_sock, &(c->origin_buf));
    if(bytes_left < 0) {
        // TODO: catch errors
        close_client(c, key->s);
    }
    // If there are no bytes left to send, I just have to wait for read
    if(bytes_left == 0) {
        if(selector_set_interest(key->s, c->origin_sock, OP_READ) != SELECTOR_SUCCESS) {
            logger(ERROR, "Error setting socket for read interest");
        }
    }
}

static ssize_t proxy_read(int sd, buffer *b) {
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


static ssize_t proxy_write(int sd, buffer *b) {
    size_t size;
    uint8_t *read_ptr = buffer_read_ptr(b, &size);
    ssize_t bytes_sent = send(sd, read_ptr, size, MSG_DONTWAIT);
    if(bytes_sent < 0) return bytes_sent;
    buffer_read_adv(b, bytes_sent);
    logger(DEBUG, "Sent %ld bytes through socket %d", bytes_sent, sd);
    return size - bytes_sent;
}

static void close_client(client *c, fd_selector s) {
    selector_unregister_fd(s, c->origin_sock);
    selector_unregister_fd(s, c->client_sock);
    freeaddrinfo(c->resolution);
    close(c->origin_sock);
    close(c->client_sock);
    clients[c->index] = NULL;
    free(c);
}
