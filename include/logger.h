#ifndef __logger_h_
#define __logger_h_
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

typedef enum {DEBUG=0, INFO, ERROR, FATAL} LOG_LEVEL;

extern LOG_LEVEL current_level;

/**
*  Minimo nivel de logger a registrar. Cualquier llamada a logger con un nivel mayor a newLevel sera ignorada
**/
void setLogLevel(LOG_LEVEL newLevel);

char * levelDescription(LOG_LEVEL level);

void logger(LOG_LEVEL level, const char *fmt, ...);

#endif
