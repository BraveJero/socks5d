#include "users.h"
#include <string.h>
#include <ctype.h>

static user users[MAX_USERS];
static size_t user_count;

static bool check_valid(const char *s, int len) {
    int i;
    for(i = 0; s[i] && i < len; i++) {
        if(s[i] == ':' || !isprint(s[i])) {
            return false;
        }
    }
    return i < len;
}

bool add_user(const char *userpass) {
    char *password = strchr(userpass, ':');
    const char *username = userpass;

    if(password == NULL) return false;
    *password = 0; 
    password++;

    if (user_count == MAX_USERS || !check_valid(username, MAX_USER_LEN) || !check_valid(password, MAX_PASS_LEN)) {
        return false;
    }

    user *user = &users[user_count];
    strcpy(user->username, username);
    strcpy(user->password, password);
    user_count++;

    return true;
}

bool try_credentials(const char *username, const char *password) {
    for(size_t i = 0; i < user_count; i++) {
        if(strcmp(users[i].username, username) == 0 && strcmp(users[i].password, password) == 0) {
            return true;
        }
    }
    return false;
}

user *get_users(size_t *len) {
    *len = user_count;
    return users;
}
