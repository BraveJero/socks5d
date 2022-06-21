#pragma once

#include <stdbool.h>
#include <stddef.h>

#define MAX_USER_LEN 10
#define MAX_PASS_LEN 20
#define MAX_USERS 20

typedef struct user {
    char username[MAX_USER_LEN + 1], password[MAX_PASS_LEN + 1];
} user;

// Agrega un usuario a la lista de usuarios. Recibe los usuarios en la forma <user>:<pass>
bool add_user(const char *userpass);

// Chequea si la contraseña es correcta para el usuario
bool try_credentials(const char *username, const char *password);

// Devuelve el puntero a un array estático de usuarios. Deja en len la cantidad.
user *get_users(size_t *len);
