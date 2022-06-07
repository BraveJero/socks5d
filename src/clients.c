#include <string.h>
#include <unistd.h>

#include "clients.h"
#include "logger.h"
#include "buffer.h"
#include "tcpClientUtil.h"
#include "util.h"
#include "tcpServerUtil.h"

static client *clients[MAX_SOCKETS];
static uint8_t buf[BUFFSIZE];

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

static fd_handler client_handler = {
    .handle_read = client_read_handler,
    .handle_write = client_write_handler,
    .handle_close = NULL,
    .handle_block = NULL,
};

static fd_handler origin_handler = {
    .handle_read = origin_read_handler,
    .handle_write = origin_write_handler,
    .handle_close = NULL,
    .handle_block = NULL,
};

// If master has activity, it must be due to incoming connections
void master_read_handler(struct selector_key *key) {
    int new_socket = acceptTCPConnection(key->fd);
    if(new_socket < 0){
        log(DEBUG, "accept() failed. New connection refused");
        return;
    }
    for(int i = 0; i < MAX_SOCKETS; i++) {
        if(clients[i] == NULL) {
            client *new_client = malloc(sizeof(client));
            if(new_client == NULL) {
                log(DEBUG, "malloc() failed. New connection refused");
                close(new_socket);
            }
            new_client->client_sock = new_socket;
            // TODO: make this non-block and catch errors.
            new_client->origin_sock = tcpClientSocket("127.0.0.1", "9999");

            buffer_init(&(new_client->client_buf), BUFFSIZE, new_client->client_buf_raw);
            buffer_init(&(new_client->origin_buf), BUFFSIZE, new_client->origin_buf_raw);
            selector_register(key->s, new_client->client_sock, &client_handler, OP_READ, new_client);
            selector_register(key->s, new_client->origin_sock, &origin_handler, OP_READ, new_client);
            new_client->index = i;
            clients[i] = new_client;

            // TODO: log client information
            log(INFO, "New connection, on socks {%d, %d}", new_socket, new_client->origin_sock);
            return;
        }
    }
}

void client_read_handler(struct selector_key *key) {
    client *c = (client *) key->data;
    ssize_t bytes_rcv = proxy_read(c->client_sock, &(c->origin_buf));
    if(bytes_rcv <= 0) {
        // TODO: catch errors
        close_client(c, key->s);
        return;
    }
    if(selector_set_interest(key->s, c->origin_sock, OP_READ | OP_WRITE) != SELECTOR_SUCCESS) {
        log(ERROR, "Error setting socket for write interest");
    }
}

void client_write_handler(struct selector_key *key) {
    client *c = (client *) key->data;
    ssize_t bytes_left = proxy_write(c->client_sock, &(c->client_buf));
    if(bytes_left < 0) {
        // TODO: catch errors
        close_client(c, key->s);
    }
    // If there are no bytes left to send, I just have to wait for read
    if(bytes_left == 0) {
        if(selector_set_interest(key->s, c->client_sock, OP_READ) != SELECTOR_SUCCESS) {
            log(ERROR, "Error setting socket for read interest");
        }
    }
}

void origin_read_handler(struct selector_key *key) {
    client *c = (client *) key->data;
    ssize_t bytes_rcv = proxy_read(c->origin_sock, &(c->client_buf));
    if(bytes_rcv <= 0) {
        // TODO: catch errors
        close_client(c, key->s);
        return;
    }
    if(selector_set_interest(key->s, c->client_sock, OP_READ | OP_WRITE) != SELECTOR_SUCCESS) {
        log(ERROR, "Error setting socket for write interest");
    }
}

void origin_write_handler(struct selector_key *key) {
    client *c = (client *) key->data;
    ssize_t bytes_left = proxy_write(c->origin_sock, &(c->origin_buf));
    if(bytes_left < 0) {
        // TODO: catch errors
        close_client(c, key->s);
    }
    // If there are no bytes left to send, I just have to wait for read
    if(bytes_left == 0) {
        if(selector_set_interest(key->s, c->origin_sock, OP_READ) != SELECTOR_SUCCESS) {
            log(ERROR, "Error setting socket for read interest");
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
    log(DEBUG, "Read %ld bytes from socket %d", bytes_rcv, sd);
    return bytes_rcv;
}


static ssize_t proxy_write(int sd, buffer *b) {
    size_t size;
    uint8_t *read_ptr = buffer_read_ptr(b, &size);
    ssize_t bytes_sent = send(sd, read_ptr, size, MSG_DONTWAIT);
    if(bytes_sent < 0) return bytes_sent;
    buffer_read_adv(b, bytes_sent);
    log(DEBUG, "Sent %ld bytes through socket %d", bytes_sent, sd);
    return size - bytes_sent;
}

static void close_client(client *c, fd_selector s) {
    selector_unregister_fd(s, c->origin_sock);
    selector_unregister_fd(s, c->client_sock);
    close(c->origin_sock);
    close(c->client_sock);
    clients[c->index] = NULL;
    free(c);
}
