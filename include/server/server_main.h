#ifndef SERVER_MAIN_H
#define SERVER_MAIN_H

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>

enum {
    BASE_TEN = 10
};

enum {
    ERROR_MESSAGE_BUFFER = 64,
    USER_MESSAGE_BUFFER = 100,
    SERVER_RESPONSE_BUFFER = 1024
};

enum {
    LOWEST_PORT_POSSIBLE = 1024,
    HIGHEST_PORT_POSSIBLE = 65535
};

typedef struct {
    int argc;
    char **argv;

    int exit_code;
    char *exit_message;

    const char *port_str;
    in_port_t port;
    struct sockaddr_storage addr;
    int sockfd;

    char user_input[USER_MESSAGE_BUFFER];
    char server_output[SERVER_RESPONSE_BUFFER];
    ssize_t bytes_received;
} server_context;


#endif /* SERVER_MAIN_H */
