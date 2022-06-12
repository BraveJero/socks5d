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
#include "args.h"

#define MAX_PENDING_CONNECTIONS 30

static bool done = false;

int main(int argc, char *argv[])
{
	close(STDIN_FILENO);
	int master[2], monitor[2], master_size = 0, monitor_size = 0;

	struct socks5args args;
	parse_args(argc, argv, &args);

	for(int i = 0; i < 2; i++) {
		if((master[master_size] = setUpMasterSocket(args.socks_port, i)) 
		   && (monitor[monitor_size] = setUpMasterSocket(args.mng_port, i)) >= 0) {
			monitor_size++; master_size++;
		} else {
			logger(ERROR, "Unable to open socket. Aborting");
			exit(1);
		}
	}

	fd_selector selector = NULL;

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

	struct fd_handler monitor_handler = {
		.handle_read = NULL,
		.handle_write = NULL,
		.handle_close = NULL,
		.handle_block = NULL
	};

	for (int i = 0; i < master_size; i++)
	{
		listen(master[i], MAX_PENDING_CONNECTIONS);
		selector_fd_set_nio(master[i]);
		selector_register(selector, master[i], &master_handler, OP_READ, NULL);
	}

	for (int i = 0; i < monitor_size; i++)
	{
		listen(monitor[i], MAX_PENDING_CONNECTIONS);
		selector_fd_set_nio(monitor[i]);
		selector_register(selector, monitor[i], &monitor_handler, OP_READ, NULL);
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
