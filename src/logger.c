#include "logger.h"
#include <time.h>

LOG_LEVEL current_level = DEBUG;


void setLogLevel(LOG_LEVEL newLevel) {
	if ( newLevel >= DEBUG && newLevel <= FATAL )
	   current_level = newLevel;
}

char * levelDescription(LOG_LEVEL level) {
    static char * description[] = {"DEBUG", "INFO", "ERROR", "FATAL"};
    if (level < DEBUG || level > FATAL)
        return "";
    return description[level];
}

void logger(LOG_LEVEL level, const char *fmt, ...){
    FILE *out = level >= ERROR? stderr : stdout;
    if(level >= current_level){
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        fprintf (out, "%d-%02d/-%02dT%02d:%02d:%02dZ\t",
                 tm->tm_year + 1900, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
        va_list arg; 
        va_start(arg, fmt); 
        vfprintf(out, fmt, arg);
        va_end(arg);
        fprintf(out,"\n");
    }
	if ( level==FATAL) exit(1);
}
