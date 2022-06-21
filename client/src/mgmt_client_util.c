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
    "DISSECTOR-STATUS\r\n",
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
    if(!get_response(sock, response_buf, BUFFSIZE, false)) {
        return false;
    }

    if (response_buf[0] == '-') {
        return false;
    }

    puts("Connection successful! Server is ready.");

    return true;
}

static bool
send_text(int sock, const char * text) {
    return send(sock, text, strlen(text), MSG_DONTWAIT) >= 0;
}

static void
simple_iteration(const char * buffer, const char * line_header) {
    char * tok = strtok(response_buf, EOL);
    tok = strtok(NULL, EOL); // All multiline responses begin with the line '+OK [...]'
    int i = 0;
    while (tok != NULL && tok[0] != '.') { // All multiline responses end with the line '.'
        printf("%s #%d: %s\n",line_header, i++, tok);
        tok = strtok(NULL, EOL);
    }
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

    puts("----------------------------");

    printf("List of capabilities:\n");
    simple_iteration(response_buf, "CAPA");

    puts("----------------------------");
    putchar('\n');

    return true;
}

bool authenticate(int sock, const char *token) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_TOKEN], token);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, false)) {
        return false;
    }

    if (response_buf[0] == '-') {
        return false;
    }

    printf("Authentication with token: \"%s\" successful!\n", token);
    return true;
}

bool stats(int sock) {
    if(!send_text(sock, commands_format[CMD_STATS])) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, true)) {
        return false;
    }
    
    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running STATS: %s\n", response_buf);
        return false;
    }

    puts("----------------------------");

    printf("List of statistics:\n");
    char * tok = strtok(response_buf, EOL);
    tok = strtok(NULL, EOL);
    while (tok != NULL && tok[0] != '.') {
        unsigned long long int stat = strtoull(tok + 1, NULL, 10);
        switch(tok[0]) {
            case 'B':
                printf("Bytes transferred: %llu\n", stat);
                break;
            case 'H':
                printf("Historical connections: %llu\n", stat);
                break;
            case 'C':
                printf("Concurrent connections: %llu\n", stat);
                break;
        }
        tok = strtok(NULL, EOL);
    }

    puts("----------------------------");
    putchar('\n');

    return true;
}

bool users(int sock) {
    if(!send_text(sock, commands_format[CMD_USERS])) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, true)) {
        return false;
    }
    
    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running USERS: %s\n", response_buf);
        return false;
    }

    puts("----------------------------");

    printf("List of users:\n");
    simple_iteration(response_buf, "USER");

    puts("----------------------------");
    putchar('\n');

    return true;
}

bool buffsize(int sock) {
    if(!send_text(sock, commands_format[CMD_BUFFSIZE])) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, true)) {
        return false;
    }
    
    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running BUFFSIZE: %s\n", response_buf);
        return false;
    }

    puts("----------------------------");

    char * tok = strtok(response_buf, EOL);
    tok = strtok(NULL, EOL);
    unsigned long long int buff_size = strtoull(tok, NULL, 10);
    printf("Current size of the buffer: %llu\n", buff_size);

    puts("----------------------------");
    putchar('\n');

    return true;
}

bool dissector_status(int sock) {
    if(!send_text(sock, commands_format[CMD_DISSECTOR_STATUS])) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, true)) {
        return false;
    }

    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running DISSECTOR-STATUS: %s\n", response_buf);
        return false;
    }

    puts("----------------------------");

    char * tok = strtok(response_buf, EOL);
    tok = strtok(NULL, EOL);
    printf("We are ");
    if (strcmp(tok, "off") == 0)
        printf("not ");
    printf("sniffing\n");

    puts("----------------------------");
    putchar('\n');

    return true;
}

bool set_buffsize(int sock, const char * size) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_SET_BUFFSIZE], size);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, false)) {
        return false;
    }

    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running SET-BUFFSIZE: %s\n", response_buf);
        return false;
    }

    puts("----------------------------");

    printf("Buffer size updated to %s\n", size);

    puts("----------------------------");
    putchar('\n');

    return true;
}

bool set_dissector_status(int sock, const char *status) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_SET_DISSECTOR_STATUS], status);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, false)) {
        return false;
    }

    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running SET-DISSECTOR-STATUS: %s\n", response_buf);
        return false;
    }
    
    puts("----------------------------");

    printf("Dissector is now %s\n", status);
    
    puts("----------------------------");
    putchar('\n');

    return true;
}

bool add_user(int sock, const char *username_password) {
    snprintf(request_buf, BUFFSIZE, commands_format[CMD_ADD_USER], username_password);
    if(!send_text(sock, request_buf)) {
        return false;
    }

    if(!get_response(sock, response_buf, BUFFSIZE, false)) {
        return false;
    }

    if (response_buf[0] == '-') {
        fprintf(stderr, "Error running ADD-USER: %s\n", response_buf);
        return false;
    }

    // TODO: Show success message with username

    return true;
}
