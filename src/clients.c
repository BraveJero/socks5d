#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "clients.h"
#include "logger.h"
#include "buffer.h"
#include "tcpClientUtil.h"
#include "util.h"
#include "tcpServerUtil.h"

#define ATTACHMENT(x) ((client *) x->data)
#define N(x) (sizeof(x)/sizeof((x)[0]))

static uint8_t buf[BUFFSIZE];

// Crea el cliente con el correspondiente sock
client *create_client(int socks);

/*
 * Lee bytes de sd y los deja en el buffer
 * Retorna la cantidad de bytes que leyó y dejó en el buffer
*/
static ssize_t read_from_sock(int sd, buffer *b);

/*
 * Lee bytes del buffer y los envía por sd
 * Retorna la cantidad de bytes que envió
*/
static ssize_t write_to_sock(int sd, buffer *b);

// Cierra todos los sockets y libera la memoria.
static void client_destroy(client *c);

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

// Definicion de las acciones de cada estado
static const struct state_definition state_actions[] = {
    {
        .state = RESOLVING,
        .on_arrival = NULL,
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
        .on_arrival = NULL,
        .on_departure = NULL,
        .on_read_ready = NULL,
        .on_write_ready = NULL,
    }, {
        .state = FAILED,
        .on_arrival = NULL,
        .on_departure = NULL,
        .on_read_ready = NULL,
        .on_write_ready = NULL,
    }
};

///////////////////////////////////////////////////////////

//:)
static void socksv5_read   (struct selector_key *key);
static void socksv5_write  (struct selector_key *key);
static void socksv5_block  (struct selector_key *key);
static void socksv5_close  (struct selector_key *key);
static const struct fd_handler socks5_handler = {
    .handle_read   = socksv5_read,
    .handle_write  = socksv5_write,
    .handle_close  = socksv5_close,
    .handle_block  = socksv5_block,
};


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
    struct selector_key *dup_key = malloc(sizeof(*key));
    
    dup_key->data = new_client;
    dup_key->fd = new_client->client_sock;
    dup_key->s = key->s;

    pthread_t tid;
    pthread_create(&tid, NULL, thread_name_resolution, dup_key);

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
    new_client->stm->max_state = FAILED;

    new_client->dest_fqdn = "pampero.itba.edu.ar";
    new_client->dest_port = 61441;

    stm_init(new_client->stm);

    buffer_init(&(new_client->client_buf), BUFFSIZE, new_client->client_buf_raw);
    buffer_init(&(new_client->origin_buf), BUFFSIZE, new_client->origin_buf_raw);
    return new_client;
}

unsigned handle_finished_resolution(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    if(c->resolution == NULL) return FAILED;
    else return CONNECTING;
}

static void socksv5_done(struct selector_key* key);

static void
socksv5_read(struct selector_key *key) {
    struct state_machine *stm   = ATTACHMENT(key)->stm;
    const enum socks5_states st = stm_handler_read(stm, key);

    if(FAILED == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_write(struct selector_key *key) {
    struct state_machine *stm   = ATTACHMENT(key)->stm;
    const enum socks5_states st = stm_handler_write(stm, key);

    if(FAILED == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_close(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    if(c != NULL) {
        client_destroy(c);
    }
}

static void
socksv5_block(struct selector_key *key) {
    struct state_machine *stm   = ATTACHMENT(key)->stm;
    const enum socks5_states st = stm_handler_block(stm, key);

    if(FAILED == st || DONE == st) {
        socksv5_done(key);
    }
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

unsigned try_connect(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    if(c->curr_addr == NULL) return FAILED;
    int sock = c->origin_sock;
    if (sock >= 0) {
        errno = 0;
        char adrr_buf[1024];
        logger(DEBUG, "%s", printAddressPort(c->curr_addr, adrr_buf));
        // Establish the connection to the server
        if ( connect(sock, c->curr_addr->ai_addr, c->curr_addr->ai_addrlen) != 0) {
            if(errno == EINPROGRESS) {
                c->origin_sock = sock;
                selector_status ss = selector_register(key->s, c->origin_sock, &socks5_handler, OP_WRITE, c);
                logger(ERROR, selector_error(ss));
                
                logger(INFO, "Connection to origin in progress");
                return CONNECTING;
            }
        }
        return FAILED;
    } else {
        logger(DEBUG, "Can't create client socket");
        return FAILED;
    }
}

unsigned origin_check_connection(struct selector_key *key) {
    client *c = ATTACHMENT(key);
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(c->origin_sock, SOL_SOCKET, SO_ERROR, &error, &len);
    if(error == 0) {
        logger(INFO, "Connection to origin succesful");

        selector_set_interest(key->s, c->origin_sock, OP_READ);
        selector_register(key->s, c->client_sock, &socks5_handler, OP_READ, c);
        return PROXY;
    } else {
        logger(ERROR, "Error: %d %s", error, strerror(error));
        c->curr_addr = c->curr_addr->ai_next;
        selector_set_interest(key->s, c->origin_sock, OP_NOOP);
        return try_connect(key);
    }
}

unsigned handle_proxy_read(struct selector_key *key) {
    ssize_t bytes_read = -1;
    client *c = ATTACHMENT(key);
    if(c->client_sock == key->fd) { // Leer del socket cliente y almacenar en el buffer del origen
        bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
        if(bytes_read == 0) return DONE;

        if(selector_set_interest(key->s, c->origin_sock, OP_WRITE | OP_READ) != SELECTOR_SUCCESS) {
            return FAILED;
        }
    } else if(c->origin_sock == key->fd) { // Leer en el socket del origen y guardar en el buffer del usuario
        bytes_read = read_from_sock(c->origin_sock, &(c->client_buf));
        if(bytes_read == 0) return DONE;

        if(selector_set_interest(key->s, c->client_sock, OP_WRITE | OP_READ) != SELECTOR_SUCCESS) {
            return FAILED;
        }
    }
    if(bytes_read < 0) return FAILED;
    return PROXY;
}

unsigned handle_proxy_write(struct selector_key *key) {
    ssize_t bytes_left = -1;
    client *c = ATTACHMENT(key);
    if(c->client_sock == key->fd) { // Envio al socket lo que haya en el buffer
        bytes_left = write_to_sock(c->client_sock, &(c->client_buf));
        if(bytes_left == 0){
            if(selector_set_interest(key->s, c->client_sock, OP_READ) != SELECTOR_SUCCESS) {
                return FAILED;
            }
        }
    } else if(c->origin_sock == key->fd) { // Leer en el socket del origen y guardar en el buffer del usuario
        bytes_left = write_to_sock(c->origin_sock, &(c->origin_buf));
        if(bytes_left == 0){
            if(selector_set_interest(key->s, c->origin_sock, OP_READ) != SELECTOR_SUCCESS) {
                return FAILED;
            }
        }
    } else {
        return FAILED;
    }

    if(bytes_left < 0) return FAILED;
    return PROXY;
}

static ssize_t read_from_sock(int sd, buffer *b) {
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


static ssize_t write_to_sock(int sd, buffer *b) {
    size_t size;
    uint8_t *read_ptr = buffer_read_ptr(b, &size);
    ssize_t bytes_sent = send(sd, read_ptr, size, MSG_DONTWAIT);
    if(bytes_sent < 0) return bytes_sent;
    buffer_read_adv(b, bytes_sent);
    logger(DEBUG, "Sent %ld bytes through socket %d", bytes_sent, sd);
    return size - bytes_sent;
}

static void client_destroy(client *c) {
    /*
     * TODO: problemita, si tenes mas de un sock para este cliente (o sea el par 
     * {cliente, origen} esta seteado en algo que no es {-1, -1}) se llama por duplicado
     * al unregister, que llama al socksv5_close que llama a la funcion que hacer frees 
     * y el sanitizer tira error porque esa memoria ya fue liberada.
    */
    // freeaddrinfo(c->resolution);
    // free(c->stm);
    // free(c);
}

static void
socksv5_done(struct selector_key* key) {
    int fds[2] = {ATTACHMENT(key)->client_sock, ATTACHMENT(key)->origin_sock};
    for(unsigned i = 0; i < N(fds); i++) {
        if(fds[i] != -1) {
            if(SELECTOR_SUCCESS != selector_unregister_fd(key->s, fds[i])) {
                abort();
            }
            close(fds[i]);
        }
    }
}
