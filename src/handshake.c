#include "handshake.h"
#include "buffer.h"
#include "clients.h"
#include "logger.h"
#include "stm.h"
#include <stddef.h>
#include <stdint.h>

const uint8_t server_version = 0x05;

static bool select_method(uint8_t method, client *c)
{
	buffer *buffer = &c->client_buf;
	buffer_write(buffer, server_version);
	buffer_write(buffer, method);

	return true;
}

unsigned read_auth_method(struct selector_key *key)
{
	client *c = ATTACHMENT(key);
	if (c->client_sock != key->fd)
		return FAILED;

	// Leer del socket cliente y almacenar en el buffer del origen
	ssize_t bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
	if (bytes_read == 0)
		return DONE;
	else if (bytes_read < 0)
		return FAILED;

	size_t availableBytes;
	uint8_t *readBuffer = buffer_read_ptr(&c->origin_buf, &availableBytes);

	// Esperar a que el mensaje inicial esté completo
	if (availableBytes < 2 || availableBytes < readBuffer[1] + 2u)
		return stm_state(c->stm);

	uint8_t version = readBuffer[0];
	uint8_t nMethods = readBuffer[1];
	uint8_t *methods = &readBuffer[2];

	if (version != server_version)
	{
		logger(ERROR, "Invalid protocol version: %u", version);
		return FAILED;
	}
	for (int i = 0; i < nMethods; i++)
	{
		switch (methods[i])
		{
		case 0x00:
			if(select_method(0, c))
				return PROXY;
			else
                return FAILED;
		}
	}

	logger(ERROR, "No valid auth method");
	return FAILED;
}
