#include "logger.h"
#include "tcpServerUtil.h"
#include "socketsIO.h"
#include "mgmt.h"
#include "selector.h"
#include "users.h"
#include "tokens.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "state.h"

#define ATTACHMENT(key) ((mgmt_client *) key->data);
#define checkEOF(count) (count == 0 || (count < 0 && errno != EAGAIN))

static char response_buf[MGMT_BUFFSIZE];
static int capa_count = 0;

static const char *success_status = "+OK";
static const char *error_status = "-ERR";
static const char *line_delimiter = "\r\n";
static const char *multiline_delimiter = ".";

static const char *hello_format = "%s mgmt server ready. Authenticate using TOKEN to access all functionalities%s";
static const char *invalid_cmd_format = "%s Invalid command for this stage %s";
static const char *invalid_arg_format = "%s Invalid arguments for command%s";
static const char *correct_password_format = "%s Welcome!%s";
static const char *incorrect_password_format = "%s Incorrect password - try again%s";
static const char *quit_format = "%s Quitting...%s";
static const char *stats_format = "%s Showing stats...%sB%lu%sH%lu%sC%lu%s%s%s";
static const char *buffsize_format = "%s %s%d%s%s%s";
static const char *set_buffsize_format = "%s Buffsize updated %s";
static const char *set_buffsize_error_format = "%s Error updating buffsize %s";
static const char *dissector_status_format = "%s %s%s%s%s%s";
static const char *set_dissector_status_format = "%s Dissector status updated %s";
static const char *set_dissector_status_error_format = "%s Error updating dissector status %s";

static const char *capa_message = "+OK \r\nTOKEN\r\nUSERS\r\nSTATS\r\nBUFFSIZE\r\nSET-BUFFSIZE\r\nDISSECTOR-STATUS\r\nSET-DISSECTOR-STATUS\r\n.\r\n";

static void handle_mgmt_read(struct selector_key *key);
static void handle_mgmt_write(struct selector_key *key);
static void handle_mgmt_close(struct selector_key *key);
static mgmt_client *create_mgmt_client(int sock);

// Guarda response en el buffer b
static ssize_t write_response(buffer *b, const char *response);

// Copia el contenido de src a dest si entra todo. src cada vacio
static ssize_t copy_from_buf(buffer *dest, buffer *src);

static const struct fd_handler mgmt_handler = {
    .handle_read   = handle_mgmt_read,
    .handle_write  = handle_mgmt_write,
    .handle_close  = handle_mgmt_close,
    .handle_block  = NULL,
};

void mgmt_master_read_handler (struct selector_key *key) {
    int new_socket = acceptTCPConnection(key->fd);
    if(new_socket < 0){
        logger(DEBUG, "accept() failed. New connection refused");
        return;
    }
            
    selector_fd_set_nio(new_socket);
    mgmt_client *new_client = create_mgmt_client(new_socket);

    if(new_client == NULL) {
        // catch error
    }

    size_t size;
    snprintf(response_buf, MGMT_BUFFSIZE, hello_format, success_status, line_delimiter);
    uint8_t *write_ptr = buffer_write_ptr(&(new_client->write_buf), &size);
    size_t len = strlen(response_buf);

    memcpy(write_ptr, response_buf, len);
    buffer_write_adv(&(new_client->write_buf), len);
    selector_register(key->s, new_socket, &mgmt_handler, OP_WRITE | OP_READ, new_client);
}

mgmt_client *create_mgmt_client(int sock) {
    mgmt_client *new_client = malloc(sizeof(mgmt_client));
    new_client->quitted = false;
    new_client->fd = sock;
    buffer_init(&(new_client->write_buf), MGMT_BUFFSIZE, new_client->write_buf_raw);
    buffer_init(&(new_client->pending_buf), MGMT_BUFFSIZE, new_client->pending_buf_raw);
    new_client->input.buf = new_client->read_buf_raw;
    new_client->input.bufSize = MGMT_BUFFSIZE;
    initState(&(new_client->input));
    return new_client;
}

static void handle_mgmt_read(struct selector_key *key) {
    mgmt_client *c = ATTACHMENT(key);

    // Si tengo comandos en el buffer de pendientes no trato de leer
    if(buffer_can_read(&(c->pending_buf))) {
        return;
    }

    if(checkEOF(fillBuffer(&(c->input), c->fd))) {
        selector_unregister_fd(key->s, c->fd);
        return;
    }
    processMgmtClient(c);
    selector_set_interest(key->s, c->fd, OP_WRITE | OP_READ);
}

static void handle_mgmt_close(struct selector_key *key) {
    close(key->fd);
    free(key->data);
}

static void handle_mgmt_write(struct selector_key *key) {
    mgmt_client *c = ATTACHMENT(key);
    if(!buffer_can_read(&(c->write_buf)) && !buffer_can_read(&(c->pending_buf)))  {
        selector_set_interest(key->s, key->fd, OP_READ);
        return;
    }

    // Si tengo algo en el buffer de pendientes, trato de copiarlo al de escritura
    if(buffer_can_read(&(c->pending_buf))){
        copy_from_buf(&(c->write_buf), &(c->pending_buf));
        processMgmtClient(c);
    }

    ssize_t bytes_left = write_to_sock(c->fd, &(c->write_buf));
    if(bytes_left == 0) {
        if(c->quitted){
            selector_unregister_fd(key->s, c->fd);
        }
    }
}

bool processMgmtClient(mgmt_client *c)
{
	char *arg;
	size_t argLen, len, response_len = 0;
    MgmtCommand cmd;
    for(; (cmd = parseMgmtRequest(&(c->input), &arg, &argLen, &len)) != MGMT_INCOMPLETE; ) {
        size_t space;
        buffer_write_ptr(&(c->write_buf), &space);
        switch (cmd)
        {
            case MGMT_INCOMPLETE: {
                return false;
            }
            case MGMT_INVALID_CMD: {
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, invalid_cmd_format, error_status, line_delimiter);
                break;
            }
            case MGMT_INVALID_ARGS: {
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, invalid_arg_format, error_status, line_delimiter);
                break;
            }
            case MGMT_CAPA: {
                logger(DEBUG, "Capa count: %d", ++capa_count);
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, "%s", capa_message);
                break;
            }
            case MGMT_TOKEN: {
                arg[argLen] = '\0';
                if(check_token(arg)) {
                    response_len = snprintf(response_buf, MGMT_BUFFSIZE, correct_password_format, success_status,  line_delimiter);
                    c->input.cond = yyctrns;
                } else {
                    response_len = snprintf(response_buf, MGMT_BUFFSIZE, incorrect_password_format, error_status, line_delimiter);
                }
                break;
            } 
            case MGMT_STATS: {
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, stats_format, success_status, line_delimiter,
                get_transferred_bytes(), line_delimiter,
                get_all_connections(), line_delimiter,
                get_current_connections(), line_delimiter,
                multiline_delimiter, line_delimiter);
                break;
            }
            case MGMT_USERS: {
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, "%s Listing users... \r\n", success_status);
                size_t user_count;
                user *users = get_users(&user_count);

                for(size_t i = 0; i < user_count; i++) {
                    logger(DEBUG, "Sending user: %s", users[i].username);
                    strcat(response_buf, users[i].username);
                    strcat(response_buf, line_delimiter);
                }
                strcat(response_buf, ".\r\n");

                break;
            }
            case MGMT_GET_BUFFSIZE: {
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, buffsize_format, success_status, line_delimiter,
                get_buffsize(), line_delimiter, multiline_delimiter, line_delimiter);
                break;
            }
            case MGMT_SET_BUFFSIZE: {
                arg[argLen] = '\0';
                size_t new_size = atoi(arg);
                if(new_size) {
                    set_buffsize(new_size);
                    response_len = snprintf(response_buf, MGMT_BUFFSIZE, set_buffsize_format, success_status, line_delimiter);
                } else {
                    response_len = snprintf(response_buf, MGMT_BUFFSIZE, set_buffsize_error_format, error_status, line_delimiter);
                }
                break;
            }
            case MGMT_GET_DISSECTOR_STATUS: {
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, dissector_status_format, success_status, line_delimiter,
                get_dissector_state()? "on" : "off", line_delimiter, multiline_delimiter, line_delimiter);
                break;
            }
            case MGMT_SET_DISSECTOR_STATUS: {
                arg[argLen] = '\0';
                for(int i = 0; arg[i]; i++) {
                    arg[i] = tolower(arg[i]);
                }
                if(strcmp(arg, "on") == 0) {
                    set_dissector_state(true);
                    response_len = snprintf(response_buf, MGMT_BUFFSIZE, set_dissector_status_format, success_status, line_delimiter);
                } else if(strcmp(arg, "off") == 0) {
                    set_dissector_state(false);
                    response_len = snprintf(response_buf, MGMT_BUFFSIZE, set_dissector_status_format, success_status, line_delimiter);
                } else {
                    response_len = snprintf(response_buf, MGMT_BUFFSIZE, set_dissector_status_error_format, error_status, line_delimiter);
                }
                break;
            }
            case MGMT_QUIT: {
                c->quitted = true;
                response_len = snprintf(response_buf, MGMT_BUFFSIZE, quit_format, success_status, line_delimiter);
                break;
            }
        }
        if(response_len > space) {
            write_response(&(c->pending_buf), response_buf);
            break;
            // No tengo mas espacio para mandar cosas, dejo de interpretar
        } else {
            write_response(&(c->write_buf), response_buf);
        }
	}
	return true;
}

static ssize_t write_response(buffer *b, const char *response) {
    size_t size, len = strlen(response);
    uint8_t *write_ptr = buffer_write_ptr(b, &size);
    if(len > size) return -1;
    memcpy(write_ptr, response, len);
    buffer_write_adv(b, len);
    return len;
}

static ssize_t copy_from_buf(buffer *dest, buffer *src) {
    size_t src_size, dest_capa;
    uint8_t *dest_ptr = buffer_write_ptr(dest, &dest_capa);
    uint8_t *src_ptr = buffer_read_ptr(src, &src_size);
    if(src_size > dest_capa) return -1;
    memcpy(dest_ptr, src_ptr, src_size);
    buffer_read_adv(src, src_size);
    buffer_write_adv(dest, src_size);
    return src_size;
}
