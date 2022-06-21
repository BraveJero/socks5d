#include "socketsIO.h"
#include "logger.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#define BUFFSIZE 2048
static char buf[BUFFSIZE];

ssize_t read_from_sock(int sd, buffer *b) {
    size_t size;
    uint8_t *write_ptr = buffer_write_ptr(b, &size);
    size = (size > BUFFSIZE? BUFFSIZE : size);
    ssize_t bytes_rcv = read(sd, buf, size);
    if(bytes_rcv > 0) {
        memcpy(write_ptr, buf, bytes_rcv);
        buffer_write_adv(b, bytes_rcv);
    }
    // logger(DEBUG, "Read %ld bytes from socket %d", bytes_rcv, sd);
    return bytes_rcv;
}


ssize_t write_to_sock(int sd, buffer *b) {
    size_t size;
    uint8_t *read_ptr = buffer_read_ptr(b, &size);
    ssize_t bytes_sent = send(sd, read_ptr, size, MSG_NOSIGNAL);
    if(bytes_sent < 0) return bytes_sent;
    buffer_read_adv(b, bytes_sent);
    // logger(DEBUG, "Sent %ld bytes through socket %d", bytes_sent, sd);
    return size - bytes_sent;
}
