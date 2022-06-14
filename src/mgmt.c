#include "logger.h"
#include "tcpServerUtil.h"
#include "socketsIO.h"
#include "mgmt.h"
#include "selector.h"
#include <stdlib.h>
#include <string.h>
#include "state.h"

#define ATTACHMENT(key) ((mgmt_client *) key->data);

static char response_buf[MGMT_BUFFSIZE];

static const char *password = "password";

static const char *success_status = "+OK";
static const char *error_status = "-ERR";
static const char *line_delimiter = "\r\n";
static const char *multiline_delimiter = ".";

static const char *hello_message = "mgmt server ready. Authenticate using PASS to access all functionalities";
static const char *invalid_cmd_message = "Invalid command for this stage";
static const char *invalid_arg_message = "Invalid arguments for command";
static const char *correct_password_message = "Welcome!";
static const char *incorrect_password_message = "Incorrect password - try again";
static const char *quit_message = "Quitting...";

static const char *stats_format = "%s Showing stats...%sBytes transferred: %lu%sHistorical connections: %lu%sCurrent connections: %lu%s%s%s";

static void handle_mgmt_read(struct selector_key *key);
static void handle_mgmt_write(struct selector_key *key);
static void handle_mgmt_close(struct selector_key *key);
static mgmt_client *create_mgmt_client(int sock);
static ssize_t write_response(buffer *b, const char *response);


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
    snprintf(response_buf, MGMT_BUFFSIZE, "%s %s%s", success_status, hello_message, line_delimiter);
    uint8_t *write_ptr = buffer_write_ptr(&(new_client->write_buf), &size);
    size_t len = strlen(response_buf);

    memcpy(write_ptr, response_buf, len);
    buffer_write_adv(&(new_client->write_buf), len);
    selector_register(key->s, new_socket, &mgmt_handler, OP_WRITE, new_client);
}

mgmt_client *create_mgmt_client(int sock) {
    mgmt_client *new_client = malloc(sizeof(mgmt_client));
    new_client->ready_to_close = false;
    new_client->fd = sock;
    buffer_init(&(new_client->write_buf), MGMT_BUFFSIZE, new_client->write_buf_raw);
    new_client->input.buf = new_client->read_buf_raw;
    new_client->input.bufSize = MGMT_BUFFSIZE;
    initState(&(new_client->input));
    return new_client;
}

static void handle_mgmt_read(struct selector_key *key) {
    mgmt_client *c = ATTACHMENT(key);
    bool complete = true;
    if(fillBuffer(&(c->input), c->fd) <= 0) {
        c->ready_to_close = true;
    } else {
        complete = processMgmtClient(c);
    }
    if(complete) {
        selector_set_interest(key->s, c->fd, OP_WRITE);
    }
}

static void handle_mgmt_close(struct selector_key *key) {
    logger(DEBUG, "Deallocating client for fd %d", key->fd);
    close(key->fd);
    free(key->data);
}

static void handle_mgmt_write(struct selector_key *key) {
    mgmt_client *c = ATTACHMENT(key);
    ssize_t bytes_left = write_to_sock(c->fd, &(c->write_buf));
    if(bytes_left == 0) {
        if(!c->ready_to_close){
            selector_set_interest(key->s, c->fd, OP_READ);
        } else {
            selector_unregister_fd(key->s, c->fd);
        }
    }
}

bool processMgmtClient(mgmt_client *c)
{
	char *arg;
	size_t argLen, len;
	MgmtCommand cmd = parseMgmtRequest(&(c->input), &arg, &argLen, &len);
	switch (cmd)
	{
		case MGMT_INCOMPLETE: {
			return false;
		}
		case MGMT_INVALID_CMD: {
			snprintf(response_buf, MGMT_BUFFSIZE, "%s %s%s", error_status, invalid_cmd_message, line_delimiter);
            break;
		}
		case MGMT_INVALID_ARGS: {
            snprintf(response_buf, MGMT_BUFFSIZE, "%s %s%s", error_status, invalid_arg_message, line_delimiter);
			break;
		}
		case MGMT_PASS: {
            arg[argLen] = '\0';
            if(strcmp(arg, password) == 0) {
                snprintf(response_buf, MGMT_BUFFSIZE, "%s %s%s", success_status, correct_password_message, line_delimiter);
                c->input.cond = yyctrns;
            } else {
                snprintf(response_buf, MGMT_BUFFSIZE, "%s %s%s", error_status, incorrect_password_message, line_delimiter);
            }
			break;
		} 
		case MGMT_STATS: {
            snprintf(response_buf, MGMT_BUFFSIZE, stats_format, success_status, line_delimiter,
            get_transferred_bytes(), line_delimiter,
            get_all_connections(), line_delimiter,
            get_current_connections(), line_delimiter,
            multiline_delimiter, line_delimiter);
			break;
		}
		case MGMT_LIST: {
            // TODO: implement users feature
			break;
		}
		case MGMT_GET_BUFFSIZE: {
			break;
		}
		case MGMT_SET_BUFFSIZE: {
			break;
		}
		case MGMT_GET_DISSECTOR_STATUS: {
			break;
		}
		case MGMT_SET_DISSECTOR_STATUS: {
			break;
		}
		case MGMT_QUIT: {
            c->ready_to_close = true;
            snprintf(response_buf, MGMT_BUFFSIZE, "%s %s%s", success_status, quit_message, line_delimiter);
			break;
		}
	}
    write_response(&(c->write_buf), response_buf);
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
