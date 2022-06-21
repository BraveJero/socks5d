#pragma once
#include <stddef.h>
#include "buffer.h"

/*
 * Lee bytes de sd y los deja en el buffer
 * Retorna la cantidad de bytes que leyó y dejó en el buffer
*/
ssize_t read_from_sock(int sd, buffer *b);

/*
 * Lee bytes del buffer y los envía por sd
 * Retorna la cantidad de bytes que quedaron en el buffer
*/
ssize_t write_to_sock(int sd, buffer *b);

