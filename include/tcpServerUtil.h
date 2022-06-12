#ifndef TCPSERVERUTIL_H_
#define TCPSERVERUTIL_H_

#include <stdio.h>
#include <sys/socket.h>


int setUpMasterSocket(uint16_t port, bool ipv6);

// Accept a new TCP connection on a server socket
int acceptTCPConnection(int servSock);

// Handle new TCP client
void handleTCPEchoClient(int clntSocket);

#endif 
