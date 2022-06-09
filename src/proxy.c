#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "logger.h"
#include "tcpServerUtil.h"
#include "tcpClientUtil.h"
#include "clients.h"

#define max(n1, n2) ((n1) > (n2) ? (n1) : (n2))

#define TRUE 1
#define FALSE 0
#define PORT_IPv4 8888
#define MAX_PENDING_CONNECTIONS 3


static bool done = false;

int main(int argc, char *argv[])
{
	int opt = true;
	int master_socket[2]; // IPv4 e IPv6 (si estan habilitados)
	int master_socket_size = 0;
	struct sockaddr_in address;
	close(STDIN_FILENO);

	fd_selector selector = NULL;

	// TODO adaptar setupTCPServerSocket para que cree socket para IPv4 e IPv6 y ademas soporte opciones (y asi no repetir codigo)

	// socket para IPv4 y para IPv6 (si estan disponibles)
	///////////////////////////////////////////////////////////// IPv4
	
	if ((master_socket[master_socket_size] = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		logger(ERROR, "socket IPv4 failed");
	}
	else
	{
		// set master socket to allow multiple connections , this is just a good habit, it will work without this
		if (setsockopt(master_socket[master_socket_size], SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
		{
			logger(ERROR, "set IPv4 socket options failed");
		}

		// type of socket created
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(PORT_IPv4);

		// bind the socket to localhost port 8888
		if (bind(master_socket[master_socket_size], (struct sockaddr *)&address, sizeof(address)) < 0)
		{
			logger(ERROR, "bind for IPv4 failed");
			close(master_socket[master_socket_size]);
		}
		else
		{
			if (listen(master_socket[0], MAX_PENDING_CONNECTIONS) < 0)
			{
				logger(ERROR, "listen on IPv4 socket failes");
				close(master_socket[master_socket_size]);
			}
			else
			{
				logger(DEBUG, "Waiting for TCP IPv4 connections on socket %d\n", master_socket[master_socket_size]);
				master_socket_size++;
			}
		}
	}

	const struct selector_init conf = {
		.signal = SIGALRM,
		.select_timeout = {
			.tv_sec = 10,
			.tv_nsec = 0,
		},
	};
	if (0 != selector_init(&conf))
	{
		logger(ERROR, "selector_init() failed");
	}

	selector = selector_new(1024);
	if (selector == NULL)
	{
		logger(ERROR, "selector_new() failed");
	}

	struct fd_handler master_handler = {
		.handle_read = master_read_handler,
		.handle_write = NULL,
		.handle_close = NULL,
		.handle_block = NULL
	};

	for (int i = 0; i < master_socket_size; i++)
	{
		selector_fd_set_nio(master_socket[i]);
		selector_register(selector, master_socket[i], &master_handler, OP_READ, NULL);
	}

	while (!done)
	{
		selector_status ss = selector_select(selector);
		if (ss != SELECTOR_SUCCESS)
		{
			logger(ERROR, "selector_select() failed. Aborting execution");
			exit(1);
		}
	}

	selector_destroy(selector);
	selector_close();

	return 0;
}
