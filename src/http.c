
#include "../include/http.h"
#include "../include/main.h"
#include "../include/network.h"
#include "../include/utils.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int  parse_http_request(server_context *ctx, client_state *state);
static int  validate_http_request(server_context *ctx, client_state *state);
static void dispatch_method(server_context *ctx, client_state *state);
static void map_url_to_path(const server_context *ctx, client_state *state);
static long parse_content_length(const char *request_buffer);

static void handle_get(server_context *ctx, client_state *state);
static void handle_head(server_context *ctx, client_state *state);
static void handle_post(server_context *ctx, client_state *state);

static int  check_file_for_get_and_head(client_state *state);
static void read_file(client_state *state);
static void send_response_headers(client_state *state, off_t file_size);
static void send_response_body(client_state *state);
static void set_status(client_state *state, int status_code);

void read_request(server_context *ctx, client_state *state)
{
    const char  *request_sentinel        = "\r\n\r\n";
    const size_t request_sentinel_length = strlen(request_sentinel);

    if(state->request_buffer)
    {
        free(state->request_buffer);
        state->request_buffer = NULL;
    }

    state->request_buffer = malloc(BASE_REQUEST_BUFFER_CAPACITY);
    if(state->request_buffer == NULL)
    {
        close_client(ctx, state);
        return;
    }
    state->request_buffer_capacity = BASE_REQUEST_BUFFER_CAPACITY;
    state->request_buffer_filled   = 0;

    int isEndOfRequest;
    do
    {
        size_t remaining_buffer_space = state->request_buffer_capacity - state->request_buffer_filled;

        if(remaining_buffer_space < REQUEST_BUFFER_INCREASE_THRESHOLD)
        {
            const size_t new_capacity       = state->request_buffer_capacity * 2;
            void        *new_buffer_pointer = realloc(state->request_buffer, new_capacity);
            if(new_buffer_pointer == NULL)
            {
                free(state->request_buffer);
                state->request_buffer_capacity = 0;
                state->request_buffer_filled   = 0;
                close_client(ctx, state);
                return;
            }
            state->request_buffer          = new_buffer_pointer;
            state->request_buffer_capacity = new_capacity;
        }

        const ssize_t result = read(state->socket, state->request_buffer + state->request_buffer_filled, 1);
        switch(result)
        {
            case -1:
                if(errno != EINTR)
                {
                    close_client(ctx, state);
                    return;
                }
                break;
            case 0:
                close_client(ctx, state);
                return;
            default:
                state->request_buffer_filled += result;
        }

        isEndOfRequest = strncmp(state->request_buffer + state->request_buffer_filled - request_sentinel_length, request_sentinel, request_sentinel_length) == 0;
    } while(!isEndOfRequest);
    state->request_buffer[state->request_buffer_filled] = '\0';
    parse_http_request(ctx, state);
}

static int parse_http_request(server_context *ctx, client_state *state)
{
    struct split_string lines;
    lines = str_split(state->request_buffer, "\r\n");

    if(lines.count < 1 || lines.strings == NULL)
    {
        free_split_string(&lines);
        send_error_response(ctx, state, HTTP_STATUS_BAD_REQUEST);
        return -1;
    }

    struct split_string mainParts = str_split(lines.strings[0], " ");
    if(mainParts.count != 3 || mainParts.strings == NULL)
    {
        free_split_string(&lines);
        free_split_string(&mainParts);
        send_error_response(ctx, state, HTTP_STATUS_BAD_REQUEST);
        return -1;
    }

    state->request.method          = strdup(mainParts.strings[0]);
    state->request.path            = strdup(mainParts.strings[1]);
    state->request.protocolVersion = strdup(mainParts.strings[2]);

    if(validate_http_request(ctx, state) != 0)
    {
        free_split_string(&mainParts);
        free_split_string(&lines);
        return -1;
    }

    for(int line = 1; line < lines.count; line++)
    {
        // lines.strings[line] // New header
    }

    dispatch_method(ctx, state);

    free_split_string(&mainParts);
    free_split_string(&lines);
    return 0;
}

static int validate_http_request(server_context *ctx, client_state *state)
{
    if(strcmp(state->request.protocolVersion, "HTTP/1.0") != 0 && strcmp(state->request.protocolVersion, "HTTP/1.1") != 0)
    {
        send_error_response(ctx, state, HTTP_STATUS_VERSION_NOT_SUPPORTED);
        return -1;
    }

    if(strcmp(state->request.method, "GET") != 0 && strcmp(state->request.method, "HEAD") != 0 && strcmp(state->request.method, "POST") != 0)
    {
        send_error_response(ctx, state, HTTP_STATUS_NOT_IMPLEMENTED);
        return -1;
    }

    return 0;
}

static void dispatch_method(server_context *ctx, client_state *state)
{
    if(strcmp(state->request.method, "GET") == 0)
    {
        handle_get(ctx, state);
    }
    else if(strcmp(state->request.method, "HEAD") == 0)
    {
        handle_head(ctx, state);
    }
    else if(strcmp(state->request.method, "POST") == 0)
    {
        handle_post(ctx, state);
    }
    else
    {
        printf("Unsupported method: %s\n", state->request.method);
    }
}

static void map_url_to_path(const server_context *ctx, client_state *state)
{
    size_t      root_directory_length;
    size_t      request_path_length;
    size_t      combined_length;
    char       *combined_path;
    char       *dir_part;
    char       *dir_real;
    char       *final_path;
    char       *last_slash;
    char       *filename_dup;
    int         path_is_valid;
    const char *req_path;

    state->file_path      = NULL;
    root_directory_length = strlen(ctx->root_directory);
    req_path              = state->request.path;
    if(req_path == NULL || req_path[0] == '\0')
    {
        return;
    }
    if(req_path[0] == '/')
    {
        req_path++;
    }
    if(req_path[0] == '\0')
    {
        return;
    }

    request_path_length = strlen(req_path);
    combined_length     = root_directory_length + 1 + request_path_length;

    combined_path = malloc(combined_length + 1);
    if(combined_path == NULL)
    {
        return;
    }

    memcpy(combined_path, ctx->root_directory, root_directory_length);
    combined_path[root_directory_length] = '/';
    memcpy(combined_path + root_directory_length + 1, req_path, request_path_length);
    combined_path[combined_length] = '\0';

    last_slash = strrchr(combined_path, '/');
    if(last_slash == NULL || last_slash[1] == '\0')
    {
        free(combined_path);
        return;
    }

    filename_dup = strdup(last_slash + 1);
    if(filename_dup == NULL)
    {
        free(combined_path);
        return;
    }

    *last_slash = '\0';
    dir_part    = strdup(combined_path);
    free(combined_path);
    if(dir_part == NULL)
    {
        free(filename_dup);
        return;
    }

    dir_real = realpath(dir_part, NULL);
    free(dir_part);
    if(dir_real == NULL)
    {
        free(filename_dup);
        return;
    }

    path_is_valid = strncmp(ctx->root_directory, dir_real, root_directory_length) == 0 && (dir_real[root_directory_length] == '\0' || dir_real[root_directory_length] == '/');

    if(path_is_valid)
    {
        size_t final_len = strlen(dir_real) + 1 + strlen(filename_dup) + 1;
        final_path       = malloc(final_len);
        if(final_path == NULL)
        {
            free(dir_real);
            free(filename_dup);
            return;
        }
        snprintf(final_path, final_len, "%s/%s", dir_real, filename_dup);
        state->file_path = final_path;
    }
    else
    {
        perror("Directory traversal attempt detected");
        state->file_path = NULL;
    }

    free(dir_real);
    free(filename_dup);
}

static void handle_get(server_context *ctx, client_state *state)
{
    printf("Handling GET request for path: %s\n", state->request.path);

    map_url_to_path(ctx, state);

    if(state->file_path == NULL)
    {
        send_error_response(ctx, state, HTTP_STATUS_NOT_FOUND);
        return;
    }

    int status;

    status = check_file_for_get_and_head(state);
    if(status == HTTP_STATUS_OK)
    {
        struct stat st;
        if(stat(state->file_path, &st) == 0)
        {
            send_response_headers(state, st.st_size);
            send_response_body(state);
        }
        else
        {
            send_error_response(ctx, state, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        }
    }
    else
    {
        send_error_response(ctx, state, status);
    }
}

static void handle_head(server_context *ctx, client_state *state)
{
    printf("Handling HEAD request for path: %s\n", state->request.path);

    map_url_to_path(ctx, state);

    if(state->file_path == NULL)
    {
        send_error_response(ctx, state, HTTP_STATUS_NOT_FOUND);
        return;
    }

    int status;

    status = check_file_for_get_and_head(state);
    if(status == HTTP_STATUS_OK)
    {
        struct stat st;
        if(stat(state->file_path, &st) == 0)
        {
            send_response_headers(state, st.st_size);
        }
        else
        {
            send_error_response(ctx, state, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        }
    }
    else
    {
        send_error_response(ctx, state, status);
    }
}

static long parse_content_length(const char *request_buffer)
{
    const char *key = "Content-Length:";
    const char *p   = request_buffer;

    while((p = strstr(p, key)) != NULL)
    {
        p += strlen(key);
        while(*p == ' ' || *p == '\t')
        {
            p++;
        }
        errno        = 0;
        char *endptr = NULL;
        long  val    = strtol(p, &endptr, BASE_TEN);
        if(errno == 0 && endptr != p && val >= 0)
        {
            return val;
        }
    }
    return -1;
}

static void handle_post(server_context *ctx, client_state *state)
{
    printf("Handling POST request for path: %s\n", state->request.path);

    long len = parse_content_length(state->request_buffer);
    if(len < 0)
    {
        set_status(state, HTTP_STATUS_BAD_REQUEST);
        send_error_response(ctx, state, HTTP_STATUS_BAD_REQUEST);
        return;
    }

    map_url_to_path(ctx, state);
    if(state->file_path == NULL)
    {
        set_status(state, HTTP_STATUS_FORBIDDEN);
        send_error_response(ctx, state, HTTP_STATUS_FORBIDDEN);
        return;
    }

    state->content_length  = (size_t)len;
    state->body_bytes_read = 0;

    state->body_fd = open(state->file_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, FILE_PERMISSIONS);
    if(state->body_fd == -1)
    {
        set_status(state, HTTP_STATUS_FORBIDDEN);
        send_error_response(ctx, state, HTTP_STATUS_FORBIDDEN);
        return;
    }

    read_file(state);
    close(state->body_fd);
    state->body_fd = -1;

    set_status(state, HTTP_STATUS_OK);
    send_response_headers(state, 0);
}

static int check_file_for_get_and_head(client_state *state)
{
    struct stat file_stat;
    if(stat(state->file_path, &file_stat) != 0)
    {
        if(errno == ENOENT)
        {
            return HTTP_STATUS_NOT_FOUND;
        }
        if(errno == EACCES)
        {
            return HTTP_STATUS_FORBIDDEN;
        }
        return HTTP_STATUS_NOT_FOUND;
    }

    if(!S_ISREG(file_stat.st_mode))
    {
        return HTTP_STATUS_FORBIDDEN;
    }

    if(access(state->file_path, R_OK) != 0)
    {
        return HTTP_STATUS_FORBIDDEN;
    }

    return HTTP_STATUS_OK;
}

static void read_file(client_state *state)
{
    char buffer[HTTP_RESPONSE_BODY_BUFFER];

    while(state->body_bytes_read < state->content_length)
    {
        size_t remaining = state->content_length - state->body_bytes_read;
        size_t chunk     = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        ssize_t r = read_full(state->socket, buffer, chunk);
        if(r != RW_SUCCESS)
        {
            return;
        }

        if(state->body_fd != -1)
        {
            if(write_full(state->body_fd, buffer, chunk) != RW_SUCCESS)
            {
                return;
            }
        }

        state->body_bytes_read += chunk;
    }
}

static void send_response_headers(client_state *state, off_t file_size)
{
    char header[HTTP_RESPONSE_HEADER_BUFFER];
    int  header_len;

    header_len = snprintf(header,
                          sizeof(header),
                          "HTTP/1.0 200 OK\r\n"
                          "Content-Length: %lld\r\n"
                          "Connection: close\r\n\r\n",
                          (long long)file_size);

    write_full(state->socket, header, (size_t)header_len);
}

static void send_response_body(client_state *state)
{
    int file_fd = open(state->file_path, O_RDONLY | O_CLOEXEC);
    if(file_fd == -1)
    {
        return;
    }

    char    buffer[HTTP_RESPONSE_BODY_BUFFER];
    ssize_t bytes_read;

    while((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
    {
        if(write_full(state->socket, buffer, (size_t)bytes_read) == -1)
        {
            close(file_fd);
            return;
        }
    }
    close(file_fd);
}

void send_error_response(server_context *ctx, client_state *state, int status_code)
{
    (void)ctx;

    const char *status_line;
    const char *body;

    state->status_code = status_code;

    switch(status_code)
    {
        case HTTP_STATUS_BAD_REQUEST:
            status_line = "HTTP/1.0 400 Bad Request\r\n";
            body        = "400 Bad Request";
            break;
        case HTTP_STATUS_FORBIDDEN:
            status_line = "HTTP/1.0 403 Forbidden\r\n";
            body        = "403 Forbidden";
            break;
        case HTTP_STATUS_NOT_FOUND:
            status_line = "HTTP/1.0 404 Not Found\r\n";
            body        = "404 Not Found";
            break;
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
            status_line = "HTTP/1.0 500 Internal Server Error\r\n";
            body        = "500 Internal Server Error";
            break;
        case HTTP_STATUS_NOT_IMPLEMENTED:
            status_line = "HTTP/1.0 501 Not Implemented\r\n";
            body        = "501 HTTP Version Not Supported";
            break;
        case HTTP_STATUS_VERSION_NOT_SUPPORTED:
            status_line = "HTTP/1.0 505 HTTP Version Not Supported\r\n";
            body        = "505 HTTP Version Not Supported";
            break;
        default:
            status_line = "HTTP/1.0 500 Internal Server Error\r\n";
            body        = "500 Internal Server Error";
            break;
    }

    char response[HTTP_ERROR_RESPONSE_BUFFER];
    int  len;

    len = snprintf(response,
                   sizeof(response),
                   "%s"
                   "Content-Length: %zu\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "%s",
                   status_line,
                   strlen(body),
                   body);

    if(len > 0)
    {
        write_full(state->socket, response, (size_t)len);
    }
}

static void set_status(client_state *state, int status_code)
{
    state->status_code = status_code;
}