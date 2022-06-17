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
#include "mgmt.h"
#include "tokens.h"

#define MAX_PENDING_CONNECTIONS 30

static bool done = false;
static void sigterm(int sig) {
	done = true;
}

int main(int argc, char *argv[])
{
	close(STDIN_FILENO);

	if(signal(SIGTERM, sigterm) == SIG_ERR) {
		logger(ERROR, "Cannot handle SIGTERM (%s). Aborting", strerror(errno));
		exit(1);
	}

	int master[2], monitor[2], master_size = 0, monitor_size = 0;

	struct socks5args args;
	parse_args(argc, argv, &args);

	char *token = getenv("TOKEN");
	if(token != NULL) add_token(token);

	for(int i = 0; i < 2; i++) {
		if((master[master_size] = setUpMasterSocket(args.socks_port, i)) >= 0 
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
		.handle_read = mgmt_master_read_handler,
		.handle_write = NULL,
		.handle_close = NULL,
		.handle_block = NULL
	};

	for (int i = 0; i < master_size; i++)
	{
		if(listen(master[i], MAX_PENDING_CONNECTIONS) < 0) {
			logger(ERROR, "Error listening on sock %d: %s", master[i], strerror(errno));
		} else {
			logger(INFO, "Waiting for TCP connections on socket %d", master[i]);
			selector_fd_set_nio(master[i]);
			selector_register(selector, master[i], &master_handler, OP_READ, NULL);
		}
	}

	for (int i = 0; i < monitor_size; i++)
	{
		if(listen(monitor[i], MAX_PENDING_CONNECTIONS) < 0) {
			logger(ERROR, "Error listening on sock %d: %s", monitor[i], strerror(errno));
		} else {
			logger(INFO, "Waiting for TCP connections on socket %d", monitor[i]);
			selector_fd_set_nio(monitor[i]);
			selector_register(selector, monitor[i], &monitor_handler, OP_READ, NULL);
		}
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

	logger(INFO, "SIGTERM received. Terminating...");

	if(selector != NULL)
		selector_destroy(selector);
		
	selector_close();

	return 0;
}
