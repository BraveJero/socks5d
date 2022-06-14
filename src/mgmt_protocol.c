#include "mgmt_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void initState(Input *in)
{
	in->tok = in->cur = in->mar = in->lim = in->buf + in->bufSize;
	in->state = -1;
	in->cond = 0;
	*in->lim = 0;
}

ssize_t fillBuffer(Input *in, int fd)
{
	const char *start = in->tok;
	const size_t shift = start - in->buf;
	const size_t free = in->bufSize - (in->lim - start);

	memmove(in->buf, start, in->bufSize - shift);
	in->lim -= shift;
	in->cur -= shift;
	in->mar -= shift;
	in->tok -= shift;

	const ssize_t count = read(fd, in->lim, free);
	if (count >= 0)
		in->lim += count;
	*in->lim = 0;

	return count;
}

