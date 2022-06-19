#pragma once
#include "selector.h"
#include "buffer.h"
#include <stdint.h>

extern const uint8_t EMPTY_IP[];

enum server_reply_type
{
    REPLY_SUCCEEDED,
    REPLY_SERVER_FAILURE,
    REPLY_NOT_ALLOWED,
    REPLY_NETWORK_UNREACHABLE,
    REPLY_HOST_UNREACHABLE,
    REPLY_CONNECTION_REFUSED,
    REPLY_TTL_EXPIRED,
    REPLY_COMMAND_NOT_SUPPORTED,
    REPLY_ADDRESS_NOT_SUPPORTED,
};

enum atyp
{
    ATYP_IPV4 = 0x01,
    ATYP_DOMAIN_NAME = 0x03,
    ATYP_IPV6 = 0x04,
    ATYP_EMPTY = ATYP_IPV4,
};

unsigned read_auth_method(struct selector_key *key);
unsigned read_plain_auth(struct selector_key *key);
unsigned read_proxy_request(struct selector_key *key);
void server_reply(buffer *b, enum server_reply_type reply, enum atyp atyp, const uint8_t *addr, uint16_t port);
