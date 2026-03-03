#include "../include/main.h"
#include "../include/http.h"
#include "../include/network.h"
#include "../include/utils.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void           parse_arguments(server_context *ctx);
static void           validate_arguments(server_context *ctx);
static server_context init_context(void);

int main(const int argc, char **argv)
{
    server_context ctx;
    ctx      = init_context();
    ctx.argc = argc;
    ctx.argv = argv;

    setup_signal_handler();
    parse_arguments(&ctx);
    validate_arguments(&ctx);
    init_server_socket(&ctx);
    init_poll_fds(&ctx);
    event_loop(&ctx);

    cleanup_server(&ctx);

    return EXIT_SUCCESS;
}

static server_context init_context(void)
{
    server_context ctx   = {0};
    ctx.argc             = 0;
    ctx.argv             = NULL;
    ctx.exit_code        = EXIT_SUCCESS;
    ctx.exit_message     = NULL;
    ctx.listen_fd        = -1;
    ctx.num_clients      = 0;
    ctx.pollfds          = NULL;
    ctx.clients          = NULL;
    ctx.pollfds_capacity = 0;
    return ctx;
}

static void parse_arguments(server_context *ctx)
{
    int         opt;
    const char *real_root_directory;
    const char *optstring = ":p:f:i:h";
    opterr                = 0;

    while((opt = getopt(ctx->argc, ctx->argv, optstring)) != -1)
    {
        // 1. Check if the argument looks like a flag (starts with -)
        if(opt != ':' && opt != '?' && opt != 'h' && optarg != NULL && optarg[0] == '-')
        {
            fprintf(stderr, "Error: Option '-%c' requires an argument.\n", opt);
            ctx->exit_code = EXIT_FAILURE;
            print_usage(ctx);
        }

        switch(opt)
        {
            case 'p':
                if(optarg == NULL)
                {
                    fprintf(stderr, "Error: Option '-p' requires an argument.\n");
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                ctx->user_entered_port = optarg;
                break;
            case 'f':
                if(optarg == NULL)
                {
                    fprintf(stderr, "Error: Option '-f' requires an argument.\n");
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                real_root_directory = realpath(optarg, NULL);
                if(real_root_directory == NULL)
                {
                    fprintf(stderr, "Error: Failed getting real path of root directory \"%s\".\n", optarg);
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                ctx->root_directory = real_root_directory;
                break;
            case 'i':
                if(optarg == NULL)
                {
                    fprintf(stderr, "Error: Option '-i' requires an argument.\n");
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                ctx->ip_address = optarg;
                break;
            case 'h':
                ctx->exit_code = EXIT_SUCCESS;
                print_usage(ctx);
            case ':':
                fprintf(stderr, "Error: Option '-%c' requires an argument.\n", optopt);
                ctx->exit_code = EXIT_FAILURE;
                print_usage(ctx);
            case '?':
                fprintf(stderr, "Error: Unknown option '-%c'.\n", optopt);
                ctx->exit_code = EXIT_FAILURE;
                print_usage(ctx);
            default:
                ctx->exit_code = EXIT_FAILURE;
                print_usage(ctx);
        }
    }
}

static void validate_arguments(server_context *ctx)
{
    if(ctx->user_entered_port == NULL)
    {
        fputs("Error: Port number is required (-p <port>).\n", stderr);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    if(ctx->root_directory == NULL)
    {
        fputs("Error: Root Directory is required (-f <dir>).\n", stderr);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    char *endptr;
    errno                           = 0;
    unsigned long user_defined_port = strtoul(ctx->user_entered_port, &endptr, PORT_INPUT_BASE);

    if(errno != 0 || *endptr != '\0' || user_defined_port > UINT16_MAX)
    {
        fprintf(stderr, "Error: Invalid port number '%s'. Must be 0-65535.\n", ctx->user_entered_port);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    ctx->port_number = (uint16_t)user_defined_port;

    struct stat st;
    if(stat(ctx->root_directory, &st) != 0)
    {
        perror("Error accessing root directory");
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    printf("SERVER START: Root directory resolved to: %s\n", ctx->root_directory);

    if(!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "Error: '%s' is not a directory.\n", ctx->root_directory);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    if(access(ctx->root_directory, R_OK | X_OK) != 0)
    {
        fprintf(stderr, "Error: No permissions to read/execute directory '%s'.\n", ctx->root_directory);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    if(ctx->ip_address == NULL)
    {
        ctx->ip_address = "127.0.0.1";
    }

    if(convert_address(ctx) == -1)
    {
        fprintf(stderr, "Error: '%s' is not a valid IPv4 or IPv6 address.\n", ctx->ip_address);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }
}

__attribute__((noreturn)) void print_usage(const server_context *ctx)
{
    fprintf(stderr, "Usage: %s -p <port> -f <root_directory> [-i <ip_address>] [-h]\n", ctx->argv[0]);
    fputs("\nOptions:\n", stderr);
    fputs("  -p <port>   Port number to listen on (Required)\n", stderr);
    fputs("  -f <path>   Path to document root (Required)\n", stderr);
    fputs("  -i <ip>     IP address to bind (Default: 127.0.0.1)\n", stderr);
    fputs("  -h          Display this help and exit\n", stderr);
    quit(ctx);
}

__attribute__((noreturn)) void quit(const server_context *ctx)
{
    if(ctx->exit_message != NULL)
    {
        fputs(ctx->exit_message, stderr);
    }
    exit(ctx->exit_code);
}