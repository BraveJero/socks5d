#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdbool.h>
#include "tcpServerUtil.h"
#include "logger.h"
#include "util.h"
#include <linux/posix_types.h>
#define MAX_PENDING_CONNECTIONS 30 // Maximum outstanding connection requests
#define BUFSIZE 256
#define MAX_ADDR_BUFFER 128

static char addrBuffer[MAX_ADDR_BUFFER];

int acceptTCPConnection(int servSock) {
	struct sockaddr_storage clntAddr; // Client address
	// Set length of client address structure (in-out parameter)
	socklen_t clntAddrLen = sizeof(clntAddr);

	// Wait for a client to connect
	int clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
	if (clntSock < 0) {
		logger(ERROR, "accept() failed");
		return -1;
	}

	// clntSock is connected to a client!
	printSocketAddress((struct sockaddr *) &clntAddr, addrBuffer);
	logger(INFO, "Handling client %s", addrBuffer);

	return clntSock;
}

int setUpMasterSocket(uint16_t port, bool ipv6) {
	int sock, opt = true;
	size_t len;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct sockaddr *addr;
	char *protocol = ipv6? "IPv6" : "IPv4";
	if ((sock = socket(ipv6? AF_INET6 : AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket()");
		logger(ERROR, "socket IPv4 failed");
		return -1;
	}
	// set master socket to allow multiple connections , this is just a good habit, it will work without this
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
	{
		logger(ERROR, "set %s socket options failed: %s", protocol, strerror(errno));
		close(sock);
		return -1;
	}

	// TODO: cambiar 15 por SO_REUSEPORT. No se por que me marca como que no esta definida.
	if (setsockopt(sock, SOL_SOCKET, 15, (char *)&opt, sizeof(opt)) < 0)
	{
		logger(ERROR, "set %s socket options failed: %s", protocol, strerror(errno));
		close(sock);
		return -1;
	}

	if(ipv6) {
		logger(INFO, "Setting ipv6 address");
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		addr6.sin6_addr = in6addr_any;
		addr6.sin6_port = htons(port);
		addr = (struct sockaddr *) &addr6;
		len = sizeof(addr6);
	} else {
		logger(INFO, "Setting ipv4 address");
		memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family =AF_INET;
		addr4.sin_addr.s_addr = INADDR_ANY;
		addr4.sin_port = htons(port);
		addr = (struct sockaddr *) &addr4;
		len = sizeof(addr4);
	}

	if (bind(sock, addr, len) < 0)
	{
		logger(ERROR, "bind for %s failed: %s", protocol, strerror(errno));
		close(sock);
		return -1;
	}

	logger(INFO, "Waiting for TCP %s connections on socket %d", protocol, sock);
	return sock;
}
