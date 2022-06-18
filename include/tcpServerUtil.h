#ifndef TCPSERVERUTIL_H_
#define TCPSERVERUTIL_H_

#include <stdint.h>
#include <stdbool.h>


int setUpMasterSocket(const char *ip, uint16_t port, bool ipv6);

// Accept a new TCP connection on a server socket
int acceptTCPConnection(int servSock);

// Handle new TCP client
void handleTCPEchoClient(int clntSocket);

#endif 
