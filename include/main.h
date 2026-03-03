#ifndef SERVER_H
#define SERVER_H

#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef NI_MAXHOST
    #define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
    #define NI_MAXSERV 32
#endif

#ifndef HTTP_STATUS_OK
    #define HTTP_STATUS_OK 200
    #define HTTP_STATUS_BAD_REQUEST 400
    #define HTTP_STATUS_FORBIDDEN 403
    #define HTTP_STATUS_NOT_FOUND 404
    #define HTTP_STATUS_INTERNAL_SERVER_ERROR 500
    #define HTTP_STATUS_NOT_IMPLEMENTED 501
    #define HTTP_STATUS_VERSION_NOT_SUPPORTED 505
#endif

enum
{
    BASE_TEN = 10
};

enum
{
    INITIAL_POLLFDS_CAPACITY = 11,
    FILE_PERMISSIONS         = 0644
};

enum
{
    HTTP_RESPONSE_HEADER_BUFFER = 256,
    HTTP_RESPONSE_BODY_BUFFER   = 4096,
    HTTP_ERROR_RESPONSE_BUFFER  = 512
};

enum
{
    RW_SUCCESS = 0,
    RW_ERROR   = -1,
    RW_EOF     = -2
};

enum {
    PORT_INPUT_BASE = 10,
    BASE_REQUEST_BUFFER_CAPACITY = 30,
    REQUEST_BUFFER_INCREASE_THRESHOLD = 20,
};

// --- STRUCTS ---

struct split_string
{
    ssize_t count;
    char  **strings;
};

struct http_request {
    char *method;
    char *path;
    char *protocolVersion;
    char **headers;
};

typedef struct http_request http_request;

struct client_state {
    int socket;
    int status_code;

    size_t request_buffer_capacity;
    size_t request_buffer_filled;
    char *request_buffer;

    size_t content_length;
    size_t body_bytes_read;
    int    body_fd;

    char *file_path;
    off_t file_size;

    http_request request;
};

typedef struct client_state client_state;

struct server_context {
    int argc;
    char **argv;

    int exit_code;
    char *exit_message;

    const char *ip_address;
    struct sockaddr_storage addr;

    int listen_fd;
    const char* user_entered_port;
    uint16_t port_number;
    const char *root_directory;

    struct pollfd *pollfds;
    nfds_t pollfds_capacity;

    nfds_t num_clients;
    struct client_state* clients;
};

typedef struct server_context server_context;

__attribute__((noreturn)) void print_usage(const server_context *ctx);
__attribute__((noreturn)) void quit(const server_context *ctx);

#endif /* SERVER_H */