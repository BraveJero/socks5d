/* Generated by re2c 3.0 */

#pragma once
#include <stddef.h>
typedef struct
{
	char *lim, *cur, *mar, *tok;
	char *buf, *writeBuf;
	int state, cond;
	size_t written, bufSize;
	
#line 13 "mgmt_protocol.re.h"
char *yyt1;
char *yyt2;
#line 13 "mgmt_protocol.re"

} Input;

#include "mgmt_protocol.h"
MgmtCommand parseMgmtRequest(Input *in, char **arg, size_t *argLen, size_t *len);

enum YYCONDTYPE {
	yycpass,
	yyctrns,
};
