#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_RESPONSE_LEN 512

typedef enum {
    COMPLETE,
    INCOMPLETE,
    ERROR,
} CommandStatus;

CommandStatus check_response(const char *response, bool multiline);
static CommandStatus check_multi_response(const char *response);
static CommandStatus check_single_response(const char *response);
