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
    SERVER_RESPONSE_BUFFER = 1024,
    PATH_BUFFER = 1024,
    MAX_ARGS = 64
};

enum {
    LOWEST_PORT_POSSIBLE = 1024,
    HIGHEST_PORT_POSSIBLE = 65535,
    FILE_PERMISSIONS = 0644
};

enum {
    MAX_CONNECTIONS = 5,
    MAX_NUM_CHILDREN = 8
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
    int clientfd;

    char user_input[USER_MESSAGE_BUFFER];
    char server_output[SERVER_RESPONSE_BUFFER];
    ssize_t bytes_received;
    pid_t worker_pids[MAX_NUM_CHILDREN];
} server_context;


#endif /* SERVER_MAIN_H */
