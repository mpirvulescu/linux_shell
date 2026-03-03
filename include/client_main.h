#ifndef CLIENT_MAIN_H
#define CLIENT_MAIN_H

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>

enum {
    BASE_TEN = 10
};

enum {
    ERROR_MESSAGE_BUFFER = 64
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

    const char *ip_address;
    const char *port_str;
    in_port_t port;
    struct sockaddr_storage addr;
    int sockfd;
} client_context;


#endif /* CLIENT_MAIN_H */





