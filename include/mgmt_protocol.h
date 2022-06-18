#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum
{
	MGMT_INVALID_CMD = 0,
	MGMT_INCOMPLETE,
	
	// Auth state
	MGMT_TOKEN,

	// Transaction state
	MGMT_STATS,
	MGMT_CAPA,
	MGMT_USERS,
	MGMT_GET_BUFFSIZE,
	MGMT_SET_BUFFSIZE,
	MGMT_GET_DISSECTOR_STATUS,
	MGMT_SET_DISSECTOR_STATUS,

	// Quit
	MGMT_QUIT,

	// Errors
	MGMT_INVALID_ARGS,
} MgmtCommand;


#include "mgmt_protocol.re.h"
ssize_t fillBuffer(Input *in, int fd);
void initState(Input *in);

