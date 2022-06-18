#pragma once

#include <stdbool.h>
#include <stddef.h>

#define MAX_LEN 25
#define MAX_USERS 30

typedef struct user {
    char username[MAX_LEN], password[MAX_LEN];
} user;

// Agrega un usuario a la lista de usuarios. Recibe los usuarios en la forma <user>:<pass>
bool add_user(const char *userpass);

// Chequea si la contraseña es correcta para el usuario
bool try_credentials(const char *username, const char *password);

// Devuelve el puntero a un array estático de usuarios. Deja en len la cantidad.
user *get_users(size_t *len);
