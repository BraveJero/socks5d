#ifndef UTIL_H_
#define UTIL_H_

#include <stdbool.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include "clients.h"


int printSocketAddress(const struct sockaddr *address, char * addrBuffer);

const char * printFamily(struct addrinfo *aip);
const char * printType(struct addrinfo *aip);
const char * printProtocol(struct addrinfo *aip);
void printFlags(struct addrinfo *aip);
char * printAddressPort( const struct addrinfo *aip, char addr[]);

// Determina si dos sockets son iguales (misma direccion y puerto)
int sockAddrsEqual(const struct sockaddr *addr1, const struct sockaddr *addr2);

void log_access_info(client *c, unsigned reply);
void log_sniffer_info(client *c, const char *user, const char *pass);

#endif 
