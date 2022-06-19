#include "handshake.h"
#include "buffer.h"
#include "clients.h"
#include "logger.h"
#include "selector.h"
#include "socketsIO.h"
#include "stm.h"
#include "users.h"
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

const uint8_t server_version = 0x05;
const uint8_t EMPTY_IP[] = {0,0,0,0};

static void simple_reply(uint8_t version, uint8_t method, client *c)
{
	buffer *buffer = &c->client_buf;
	buffer_write(buffer, version);
	buffer_write(buffer, method);
}

unsigned read_auth_method(struct selector_key *key)
{
	client *c = ATTACHMENT(key);
	assert(c->client_sock == key->fd);

	// Leer del socket cliente y almacenar en el buffer del origen
	ssize_t bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
	if (checkEOF(bytes_read))
		return closeClient(c,  CLIENT_READ, key);

	size_t availableBytes;
	uint8_t *readBuffer = buffer_read_ptr(&c->origin_buf, &availableBytes);

	// Esperar a que el mensaje inicial esté completo
	if (availableBytes < 2 || availableBytes < readBuffer[1] + 2u)
		return stm_state(c->stm);

	uint8_t version = readBuffer[0];
	uint8_t nMethods = readBuffer[1];
	uint8_t *methods = &readBuffer[2];
	buffer_read_adv(&c->origin_buf, readBuffer[1] + 2);

	if (version != server_version)
	{
		logger(ERROR, "Invalid protocol version: %u", version);
		goto fail;
	}
	size_t nUsers = 1;
	get_users(&nUsers);

	for (int i = 0; i < nMethods; i++)
	{
		switch (methods[i])
		{
		case 0x0:
			if(nUsers != 0)
				break;
			simple_reply(server_version, 0x0, c);
			selector_add_interest(key->s, c->client_sock, OP_WRITE);
			return REQUEST;
		case 0x2:
			if(nUsers == 0)
				break;
			simple_reply(server_version, 0x2, c);
			selector_add_interest(key->s, c->client_sock, OP_WRITE);
			return PLAIN_AUTH;
		}
	}

	logger(ERROR, "No valid auth method");
	fail:
	simple_reply(server_version, 0xFF, c);
	return closeClient(c,  CLIENT_READ, key);
}

unsigned read_plain_auth(struct selector_key *key)
{
	const uint8_t plain_auth_version = 0x01;

	client *c = ATTACHMENT(key);
	assert(c->client_sock == key->fd);

	// Leer del socket cliente y almacenar en el buffer del origen
	ssize_t bytes_read = read_from_sock(c->client_sock, &(c->origin_buf));
	if (checkEOF(bytes_read))
		return closeClient(c,  CLIENT_READ, key);

	size_t availableBytes;
	uint8_t *readBuffer = buffer_read_ptr(&c->origin_buf, &availableBytes);

	// Esperar a que el mensaje esté completo
	if (availableBytes < 2 || availableBytes < 2u + readBuffer[1])
		return stm_state(c->stm);

	uint8_t version = readBuffer[0];
	uint8_t userLen = readBuffer[1];
	uint8_t *user = &readBuffer[2];
	uint8_t passLen = readBuffer[2 + userLen];
	uint8_t *pass = &readBuffer[3 + userLen];

	// Esperar a que el mensaje esté completo
	if(availableBytes < 3u + userLen + passLen)
		return stm_state(c->stm);

	buffer_read_adv(&c->origin_buf, 3 + userLen + passLen);
	if(version != plain_auth_version)
	{
		logger(ERROR, "Invalid protocol version: %u", version);
		simple_reply(plain_auth_version, 0xFF, c);
		return closeClient(c,  CLIENT_READ, key);
	}

    char pass_copy[MAX_CREDENTIALS];

	memcpy(c->socks_user, user, userLen);
	c->socks_user[userLen] = 0;
	memcpy(pass_copy, pass, passLen);
    pass_copy[passLen] = 0;
	if(try_credentials(c->socks_user, pass_copy))
	{
		simple_reply(plain_auth_version, 0x0, c);
		selector_add_interest(key->s, c->client_sock, OP_WRITE);
		return REQUEST;
	}
	else
	{
		simple_reply(plain_auth_version, 0x1, c);
		return closeClient(c, CLIENT_READ, key);
	}
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
	buffer_write_adv(b, addrLen);
	memcpy(&buf[addrLen], &port, sizeof(port));
	buffer_write_adv(b, sizeof(port));
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
		return closeClient(c,  CLIENT_READ, key);

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
		default:
			server_reply(&c->client_buf, REPLY_ADDRESS_NOT_SUPPORTED, ATYP_IPV4, EMPTY_IP, 0);
			return closeClient(c,  CLIENT_READ, key);
	}
	if(availableBytes < 6u + addrLen)
		return stm_state(c->stm);

	buffer_read_adv(&c->origin_buf, addrLen + 6);

	enum cmd cmd = readBuffer[1];
	if(cmd != CONNECT)
	{
		server_reply(&c->client_buf, REPLY_COMMAND_NOT_SUPPORTED, ATYP_IPV4, EMPTY_IP, 0);
		return closeClient(c,  CLIENT_READ, key);
	}

	if(atyp == ATYP_DOMAIN_NAME)
	{
		c->dest_fqdn = malloc(addrLen);

		memcpy(c->dest_fqdn, &readBuffer[5], addrLen-1);
		c->dest_fqdn[addrLen-1] = '\0';
		c->dest_port = *(uint16_t*)&readBuffer[4 + addrLen];
		return RESOLVING;
	}
	else if(atyp == ATYP_IPV4)
	{
		c->curr_addr = malloc(sizeof(struct addrinfo));
		struct sockaddr_in *sock = malloc(sizeof(*sock));
		*sock = (struct sockaddr_in)
		{
			.sin_family = AF_INET,
			.sin_addr = *(struct in_addr*)&readBuffer[4],
			.sin_port = *(in_port_t*)&readBuffer[4 + addrLen],
		};
		*c->curr_addr = (struct addrinfo)
		{
			.ai_family = AF_INET,
			.ai_socktype = SOCK_STREAM,
			.ai_addr = (struct sockaddr*)sock,
			.ai_addrlen = sizeof(*sock),
		};
		return CONNECTING;
	}
	else // if(atyp == ATYP_IPV6)
	{
		c->curr_addr = malloc(sizeof(struct addrinfo));
		struct sockaddr_in6 *sock = malloc(sizeof(*sock));
		*sock = (struct sockaddr_in6)
		{
			.sin6_family = AF_INET6,
			.sin6_addr = *(struct in6_addr*)&readBuffer[4],
			.sin6_port = *(in_port_t*)&readBuffer[4 + addrLen],
		};
		*c->curr_addr = (struct addrinfo)
		{
			.ai_family = AF_INET6,
			.ai_socktype = SOCK_STREAM,
			.ai_addr = (struct sockaddr*)sock,
			.ai_addrlen = sizeof(*sock),
		};
		return CONNECTING;
	}
}
