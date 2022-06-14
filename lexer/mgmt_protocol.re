#include "../include/mgmt_protocol.re.h"
#include <stdbool.h>
#include <string.h>
/*!header:re2c:on*/
#pragma once
#include <stddef.h>
typedef struct
{
	char *lim, *cur, *mar, *tok;
	char *buf, *writeBuf;
	int state, cond;
	size_t written, bufSize;
	/*!stags:re2c format = 'char *@@;\n'; */
} Input;

#include "../include/mgmt_protocol.h"
MgmtCommand parseMgmtRequest(Input *in, char **arg, size_t *argLen, size_t *len);
/*!header:re2c:off*/

/*!re2c
    re2c:api:style = free-form;
    re2c:define:YYCTYPE = "char";
*/

MgmtCommand parseMgmtRequest(Input *in, char **arg, size_t *argLen, size_t *len)
{
	char yych;
	char *args = NULL, *arge = NULL, *end;
	MgmtCommand result;
	/*!stags:re2c:request format = '#define @@ in->@@\n'; */

#define YYGETSTATE in->state
/*!getstate:re2c:request*/
#undef YYGETSTATE

	for (;;)
	{
		/*!local:re2c:request
        re2c:flags:tags = 1;
        re2c:eof = 0;
        re2c:define:YYCURSOR   = "in->cur";
        re2c:define:YYMARKER   = "in->mar";
        re2c:define:YYLIMIT    = "in->lim";
        re2c:define:YYSETSTATE = "in->state = @@;";
        re2c:define:YYGETCONDITION = "in->cond";
        re2c:define:YYSETCONDITION = "in->cond = @@;";
        re2c:define:YYFILL     = "{ return MGMT_INCOMPLETE; }";

        print = [\x20-\x7f];
        arg = (print\[ ])+;
        numarg = ([0-9])+;
        cmds = ('pass'|'users'|'list'|'buffsize'|'set-buffsize'|'dissector-status'|'set-dissector-status');

        <pass> 'pass' [ ]+ @args arg @arge [ ]* '\r\n'                      { result = MGMT_PASS; break; }
        
        <trns> 'stats' [ ]* '\r\n'                                          { result = MGMT_STATS; break; }
        <trns> 'users' [ ]* '\r\n'                                          { result = MGMT_USERS; break; }
        <trns> 'buffsize' [ ]* '\r\n'                                       { result = MGMT_GET_BUFFSIZE; break; }
        <trns> 'set-buffsize' [ ]+ @args numarg @arge [ ]* '\r\n'           { result = MGMT_SET_BUFFSIZE; break; }
        <trns> 'dissector-status' [ ]* '\r\n'                               { result = MGMT_GET_DISSECTOR_STATUS; break; }
        <trns> 'set-dissector-status' [ ]* @args ("on"|"off") @arge'\r\n'   { result = MGMT_SET_DISSECTOR_STATUS; break; }
        
        <*> 'quit' [ ]* '\r\n'                                              { result = MGMT_QUIT; break; }
        <*> 'capa' [ ]* '\r\n'                                              { result = MGMT_CAPA; break; }

        <*> cmds [ ]+ [^\r]* '\r\n'                                         { result = MGMT_INVALID_ARGS; break; }
        <*> print+ '\r\n'                                                   { result = MGMT_INVALID_CMD; break; }
        <*> '\r\n'                                                          { result = MGMT_INVALID_CMD; break; }
        <*> *                                                               { result = MGMT_INVALID_CMD; break; }
        <*> $                                                               { result = MGMT_INCOMPLETE; break; }
    */
	}

	end = in->cur;
	*arg = args;
	*argLen = arge - args;
	*len = end - in->tok;
	in->state = -1;
	return result;
}