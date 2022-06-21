#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>

#include "mgmt_client_util.h"

#define BUFFSIZE MAX_RESPONSE_LEN
static char response_buf[BUFFSIZE];
static char request_buf[BUFFSIZE];

// typedef struct command_type {
//     uint8_t id;
//     const char * format;
//     bool multiline;
// } command_type;

static const char *commands_format[] = {
    "CAPA\r\n",
    "TOKEN %s\r\n",
    "STATS\r\n",
    "USERS\r\n",
    "BUFFSIZE\r\n",
    "DISSECTOR_STATUS\r\n",
    "SET-BUFFSIZE %s\r\n", // We let the server parse the number
    "SET-DISSECTOR-STATUS %s\r\n",
    "ADD-USER %s\r\n",
};

//static const bool commands_multilne[] = {
//        true,
//        false,
//        true,
//        true,
//        true,
//        true,
//        false,
//        false,
//};

int tcpClientSocket(const char *host, const char *service) {
    struct addrinfo addrCriteria;                   // Criteria for address match
    memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
    addrCriteria.ai_family = AF_UNSPEC;             // v4 or v6 is OK
    addrCriteria.ai_socktype = SOCK_STREAM;         // Only streaming sockets
    addrCriteria.ai_protocol = IPPROTO_TCP;         // Only TCP protocol

    // Get address(es)
    struct addrinfo *servAddr; // Holder for returned list of server addrs
    int rtnVal = getaddrinfo(host, service, &addrCriteria, &servAddr);
    if (rtnVal != 0) {
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *addr = servAddr; addr != NULL && sock == -1; addr = addr->ai_next) {
        // Create a reliable, stream socket using TCP
        sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sock >= 0) {
            errno = 0;
            // Establish the connection to the server
            if ( connect(sock, addr->ai_addr, addr->ai_addrlen) != 0) {
                close(sock); 	// Socket connection failed; try next address
                sock = -1;
            }
        }
    }

    freeaddrinfo(servAddr);
    return sock;
}

bool read_hello(int sock) {
    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }
    response_buf[bytes_read] = '\0';

    return response_buf[0] == '+';
}

static bool
send_text(int sock, const char * text) {
    return send(sock, text, strlen(text), MSG_DONTWAIT) >= 0;
}

/*
 * buff is expected to hold a null terminated string
 */
static bool
finished(char * buff, bool multiline) {
    if (buff[0] == '-') // We know errors are one line
        multiline = false;
    return strstr(buff, (multiline) ? EOM : EOL) != NULL;
}

/*
 * Returns true if answer (whether +OK or -ERR is received) could be read.
 * Returns false if an error occured.
 */
static bool
get_response(int sock, char * buff, size_t len, bool multiline) {
    char * write_ptr = buff;
    do {
        uint8_t bytes_read = read(sock, write_ptr, len - (write_ptr - buff) );
        if (bytes_read <= 0) {
            fprintf(stderr, "Error while reading answer.");
            return false;
        }
        write_ptr += bytes_read;
        *write_ptr = '\0';
    } while(!finished(buff, multiline));
    return true;
}

bool capa(int sock) {
    if(!send_text(sock, commands_format[CMD_CAPA])) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, true)) {
        return false;
    }
    
    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running CAPA: %s\n", response_buf);
        return false;
    }

    printf("List of capabilities:\n");
    char * cap = strtok(response_buf, EOL);
    cap = strtok(NULL, EOL);
    int i = 0;
    while (cap != NULL && cap[0] != '.') {
        printf("CAPA #%d: %s\n", i++, cap);
        cap = strtok(NULL, EOL);
    }

    return true;
}

bool authenticate(int sock, const char *token) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_TOKEN], token);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    return response_buf[0] == '+';
}

bool stats(int sock) {
    if(!send_text(sock, commands_format[CMD_STATS])) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    printf("{{%s}}\n", response_buf);
    return response_buf[0] == '+';
}

bool users(int sock) {
    if(!send_text(sock, commands_format[CMD_USERS])) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    printf("{{%s}}\n", response_buf);
    return response_buf[0] == '+';
}

bool buffsize(int sock) {
    if(!send_text(sock, commands_format[CMD_BUFFSIZE])) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    printf("{{%s}}\n", response_buf);
    return response_buf[0] == '+';
}

bool dissector_status(int sock) {
    if(!send_text(sock, commands_format[CMD_DISSECTOR_STATUS])) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    printf("{{%s}}\n", response_buf);
    return response_buf[0] == '+';
}

bool set_buffsize(int sock, size_t size) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_SET_BUFFSIZE], size);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    return response_buf[0] == '+';
}

bool set_dissector_status(int sock, const char *status) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_SET_DISSECTOR_STATUS], status);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    return response_buf[0] == '+';
}


bool add_user(int sock, const char *username_password) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_ADD_USER], username_password);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    ssize_t bytes_read;
    if((bytes_read = read(sock, response_buf, BUFFSIZE)) <= 0) {
        return false;
    }

    response_buf[bytes_read] = '\0';
    return response_buf[0] == '+';
}
