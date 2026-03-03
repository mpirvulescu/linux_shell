#include "../include/network.h"
#include "../include/http.h"    // For read_request()
#include "../include/main.h"    // For quit()
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables
static volatile sig_atomic_t exit_flag = 0;

static void signal_handler(int sig);
static void accept_client(server_context *ctx);

void setup_signal_handler(void)
{
    struct sigaction sa = {0};
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = signal_handler;
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
    sigaction(SIGINT, &sa, NULL);
}

static void signal_handler(int sig)
{
    (void)sig;
    exit_flag = 1;
}

void init_server_socket(server_context *ctx)
{
    int sockfd;
    sockfd = socket(ctx->addr.ss_family, SOCK_STREAM, 0);

    if(sockfd == -1)
    {
        fprintf(stderr, "Error: socket could not be created\n");
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    if(fcntl(sockfd, F_SETFD, FD_CLOEXEC) == -1)
    {
        fprintf(stderr, "Error: fcntl failed\n");
        close(sockfd);
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    int enable = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1)
    {
        fprintf(stderr, "Error: setsockopt failed\n");
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port = htons(ctx->port_number);

    if(ctx->addr.ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr = (struct sockaddr_in *)&ctx->addr;
        addr_len                      = sizeof(*ipv4_addr);
        ipv4_addr->sin_port           = net_port;
        vaddr                         = (void *)&(((struct sockaddr_in *)&ctx->addr)->sin_addr);
    }
    else if(ctx->addr.ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr = (struct sockaddr_in6 *)&ctx->addr;
        addr_len                       = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port           = net_port;
        vaddr                          = (void *)&(((struct sockaddr_in6 *)&ctx->addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr, "Error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", ctx->addr.ss_family);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    if(inet_ntop(ctx->addr.ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        fprintf(stderr, "Error: inet_ntop failed\n");
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    printf("Binding to %s:%u\n", addr_str, ctx->port_number);

    if(bind(sockfd, (struct sockaddr *)&ctx->addr, addr_len) == -1)
    {
        fprintf(stderr, "Error: binding to the socket failed\n");
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    printf("Bound to socket: %s:%u\n", addr_str, ctx->port_number);

    if(listen(sockfd, SOMAXCONN) == -1)
    {
        fprintf(stderr, "Listening failed\n");
        close(sockfd);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    printf("Listening for incoming connections...\n");
    ctx->listen_fd = sockfd;
}

void init_poll_fds(struct server_context *ctx)
{
    ctx->pollfds_capacity = INITIAL_POLLFDS_CAPACITY;

    ctx->clients = malloc(sizeof(client_state) * ctx->pollfds_capacity);
    if(ctx->clients == NULL)
    {
        perror("Error: client malloc failed");
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    for(nfds_t i = 0; i < ctx->pollfds_capacity; i++)
    {
        ctx->clients[i].socket = -1;
    }

    ctx->pollfds = malloc(sizeof(struct pollfd) * (ctx->pollfds_capacity + 1));
    if(ctx->pollfds == NULL)
    {
        perror("Error: pollfds malloc failed");
        free(ctx->clients);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    ctx->pollfds[0].fd      = ctx->listen_fd;
    ctx->pollfds[0].events  = POLLIN;
    ctx->pollfds[0].revents = 0;

    for(nfds_t i = 1; i <= ctx->pollfds_capacity; i++)
    {
        ctx->pollfds[i].fd      = -1;
        ctx->pollfds[i].events  = 0;
        ctx->pollfds[i].revents = 0;
    }

    ctx->num_clients = 0;
}

void event_loop(server_context *ctx)
{
    while(!exit_flag)
    {
        int activity = poll(ctx->pollfds, ctx->num_clients + 1, -1);

        if(activity < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            perror("Error: poll failed");
            ctx->exit_code = EXIT_FAILURE;
            return;
        }

        if(ctx->pollfds[0].revents & POLLIN)
        {
            accept_client(ctx);
        }

        for(nfds_t i = ctx->num_clients; i > 0; i--)
        {
            nfds_t client_index = i - 1;
            nfds_t poll_index   = client_index + 1;

            if(ctx->pollfds[poll_index].revents & POLLIN)
            {
                read_request(ctx, &ctx->clients[client_index]);
            }
        }
    }
}

void cleanup_server(const server_context *ctx)
{
    if(ctx->pollfds)
    {
        free(ctx->pollfds);
    }

    if(ctx->clients)
    {
        for(nfds_t i = 0; i < ctx->num_clients; i++)
        {
            if(ctx->clients[i].request_buffer)
            {
                free(ctx->clients[i].request_buffer);
            }
            if(ctx->clients[i].request.method)
            {
                free(ctx->clients[i].request.method);
            }
            if(ctx->clients[i].request.path)
            {
                free(ctx->clients[i].request.path);
            }
            if(ctx->clients[i].request.protocolVersion)
            {
                free(ctx->clients[i].request.protocolVersion);
            }
            if(ctx->clients[i].file_path)
            {
                free(ctx->clients[i].file_path);
            }
        }
        free(ctx->clients);
    }

    if(ctx->listen_fd != -1)
    {
        close(ctx->listen_fd);
    }
}

static void accept_client(server_context *ctx)
{
    struct sockaddr_storage client_addr;
    socklen_t               addr_len = sizeof(client_addr);
    int                     client_fd;

    char client_host[NI_MAXHOST];
    char client_service[NI_MAXSERV];

    errno     = 0;
    client_fd = accept(ctx->listen_fd, (struct sockaddr *)&client_addr, &addr_len);

    if(client_fd == -1)
    {
        if(errno == EINTR)
        {
            perror("Accept failed");
        }
        return;
    }

    if(getnameinfo((struct sockaddr *)&client_addr, addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted a new connection from %s:%s\n", client_host, client_service);
    }
    else
    {
        printf("unable to get client information\n");
    }

    if(ctx->num_clients >= ctx->pollfds_capacity)
    {
        size_t new_capacity = (ctx->num_clients + 1) * 2;

        struct pollfd *new_poll = realloc(ctx->pollfds, sizeof(struct pollfd) * (new_capacity + 1));
        if(!new_poll)
        {
            perror("Error: realloc pollfds failed");
            close(client_fd);
            return;
        }
        ctx->pollfds = new_poll;

        client_state *new_clients = realloc(ctx->clients, sizeof(client_state) * new_capacity);
        if(!new_clients)
        {
            perror("Error: realloc clients failed");
            close(client_fd);
            return;
        }
        ctx->clients = new_clients;

        for(nfds_t i = ctx->pollfds_capacity + 1; i <= new_capacity; i++)
        {
            ctx->pollfds[i].fd      = -1;
            ctx->pollfds[i].events  = 0;
            ctx->pollfds[i].revents = 0;
        }
        ctx->pollfds_capacity = new_capacity;
    }

    nfds_t poll_index   = ctx->num_clients + 1;
    nfds_t client_index = ctx->num_clients;

    ctx->pollfds[poll_index].fd      = client_fd;
    ctx->pollfds[poll_index].events  = POLLIN;
    ctx->pollfds[poll_index].revents = 0;

    memset(&ctx->clients[client_index], 0, sizeof(client_state));
    ctx->clients[client_index].socket  = client_fd;
    ctx->clients[client_index].body_fd = -1;

    ctx->num_clients++;
}

void close_client(server_context *ctx, const client_state *state)
{
    if(state->socket != -1)
    {
        close(state->socket);
    }

    if(state->request_buffer)
    {
        free(state->request_buffer);
    }

    if(state->file_path)
    {
        free(state->file_path);
    }

    if(state->request.method)
    {
        free(state->request.method);
    }

    if(state->request.path)
    {
        free(state->request.path);
    }

    if(state->request.protocolVersion)
    {
        free(state->request.protocolVersion);
    }

    if(state->body_fd != -1)
    {
        close(state->body_fd);
    }

    nfds_t client_index = state - ctx->clients;

    if((size_t)client_index >= ctx->num_clients)
    {
        return;
    }

    size_t items_to_move = ctx->num_clients - client_index - 1;

    if(items_to_move > 0)
    {
        memmove(&ctx->clients[client_index], &ctx->clients[client_index + 1], sizeof(client_state) * items_to_move);
        memmove(&ctx->pollfds[client_index + 1], &ctx->pollfds[client_index + 2], sizeof(struct pollfd) * items_to_move);
    }

    ctx->num_clients--;

    printf("Safely removed client connection\n");
}