#include "handshake.h"
#include "buffer.h"
#include "clients.h"
#include "logger.h"
#include "selector.h"
#include "stm.h"
#include <netdb.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

const uint8_t server_version = 0x05;

static void select_method(uint8_t method, client *c)
{
	buffer *buffer = &c->client_buf;
	buffer_write(buffer, server_version);
	buffer_write(buffer, method);
}

unsigned read_auth_method(struct selector_key *key)
{
	client *c = ATTACHMENT(key);
	assert(c->client_sock == key->fd);

	// Leer del socket cliente y almacenar en el buffer del origen
	ssize_t bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
	if (checkEOF(bytes_read))
		return closeClient(c, CLIENT_READ, key->s);

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
		goto fail;
	}	
	for (int i = 0; i < nMethods; i++)
	{
		switch (methods[i])
		{
		case 0x00:
			select_method(0, c);
			break;
		}
	}

	logger(ERROR, "No valid auth method");
	fail:
	select_method(0xFF, c);
	return closeClient(c, CLIENT_READ, key->s);
}

void server_reply(buffer *b, enum server_reply_type reply, enum atyp atyp, const uint8_t *addr, uint16_t port)
{
	buffer_write(b, server_version);
	buffer_write(b, reply);
	buffer_write(b, 0);
	buffer_write(b, atyp);
	size_t space;
	uint8_t *buf = buffer_write_ptr(b, &space);
	size_t addrLen;
	switch (atyp)
	{
		case ATYP_IPV4:
			addrLen = 4;
			break;
		case ATYP_IPV6:
			addrLen = 16;
			break;
		case ATYP_DOMAIN_NAME:
			addrLen = strlen((char*)addr);
			break;
	}
	if(space < addrLen + sizeof(port))
		return;
	memcpy(buf, addr, addrLen);
	memcpy(&buf[addrLen], &port, sizeof(port));
}

unsigned read_proxy_request(struct selector_key *key)
{
	enum cmd
	{
		CONNECT = 0x01,
		BIND = 0x02,
		UDP_ASSOCIATE = 0x03,
	};

	client *c = ATTACHMENT(key);
	assert(c->client_sock == key->fd);

	// Leer del socket cliente y almacenar en el buffer del origen
	ssize_t bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
	if (checkEOF(bytes_read))
		return closeClient(c, CLIENT_READ, key->s);

	size_t availableBytes;
	uint8_t *readBuffer = buffer_read_ptr(&c->origin_buf, &availableBytes);

	// Esperar a que el mensaje inicial esté completo
	// Leer 4 bytes del mensaje y 1 de la direccion por si es un DN
	if (availableBytes < 5)
		return stm_state(c->stm);
	enum atyp atyp = readBuffer[3];
	size_t addrLen;
	switch (atyp)
	{
		case ATYP_IPV4:
			addrLen = 4;
			break;
		case ATYP_IPV6:
			addrLen = 16;
			break;
		case ATYP_DOMAIN_NAME:
			addrLen = 1 + readBuffer[4];
			break;
	}
	if(availableBytes < 6u + addrLen)
		return stm_state(c->stm);

	enum cmd cmd = readBuffer[1];
	if(cmd != CONNECT)
	{
		server_reply(&c->client_buf, REPLY_COMMAND_NOT_SUPPORTED, ATYP_IPV4, (uint8_t[]){0,0,0,0}, 0);
		return closeClient(c, CLIENT_READ, key->s);
	}

	if(atyp == ATYP_DOMAIN_NAME)
	{
		c->dest_fqdn = malloc(addrLen);

		memcpy(c->dest_fqdn, &readBuffer[5], addrLen-1);
		c->dest_fqdn[addrLen-1] = '\0';
		return RESOLVING;
	}
	else
	{
		c->curr_addr = calloc(1, sizeof(struct addrinfo));
		struct sockaddr *sock = malloc(sizeof(struct sockaddr));

		memcpy(sock, &readBuffer[4], addrLen);
		c->curr_addr->ai_addr = sock;
		c->curr_addr->ai_addrlen = addrLen;
		return CONNECTING;
	}
}
