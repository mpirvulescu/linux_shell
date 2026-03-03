#include "client_main.h"
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void           parse_arguments(client_context *ctx);
static void           handle_arguments(client_context *ctx);
static client_context init_context(void);
// this is just used by handle_arguments
static in_port_t      parse_in_port_t(client_context *ctx);
_Noreturn static void usage(const client_context *ctx);
_Noreturn static void quit(const client_context *ctx);
static void           convert_address(client_context *ctx);
static int            socket_create();
static void           socket_connect();
/*
    all code once connected to server
    happens here, after connection established
*/
static void shutdown_socket();
static void socket_close();

int main(const int argc, char **argv)
{
    client_context ctx;
    ctx      = init_context();
    ctx.argc = argc;
    ctx.argv = argv;

    parse_arguments(&ctx);
    handle_arguments(&ctx);
    convert_address(&ctx);
    // sockfd = socket_create(&ctx);
    // socket_connect(&ctx);
    // // add more client specific code here

    // shutdown_socket(&ctx);
    // socket_close(&ctx);
}

static client_context init_context(void)
{
    client_context ctx = {0};
    ctx.argc           = 0;
    ctx.argv           = NULL;
    ctx.exit_code      = EXIT_SUCCESS;
    ctx.exit_message   = NULL;
    ctx.sockfd         = -1;
    return ctx;
}

static void parse_arguments(client_context *ctx)
{
    int         opt;
    const char *optstring = ":i:p:h";
    opterr                = 0;

    while((opt = getopt(ctx->argc, ctx->argv, optstring)) != -1)
    {
        if(opt != ':' && opt != '?' && opt != 'h' && optarg != NULL && optarg[0] == '-')
        {
            fprintf(stderr, "Error: Option '-%c' requires an argument\n", opt);
            ctx->exit_code = EXIT_FAILURE;
            usage(ctx);
        }

        switch(opt)
        {
            case 'p':
                ctx->port_str = optarg;
                break;
            case 'i':
                ctx->ip_address = optarg;
                break;
            case 'h':
                ctx->exit_code = EXIT_SUCCESS;
                usage(ctx);
                break;
            case ':':
                fprintf(stderr, "Error: Option '-%c' requires an argument.\n", optopt);
                ctx->exit_code = EXIT_FAILURE;
                usage(ctx);
                break;
            case '?':
                fprintf(stderr, "Error: Unknown option '-%c'\n", optopt);
                ctx->exit_code = EXIT_FAILURE;
                usage(ctx);
            default:
                ctx->exit_code = EXIT_FAILURE;
                usage(ctx);
        }
    }
}

static void handle_arguments(client_context *ctx)
{
    if(ctx->ip_address == NULL)
    {
        fprintf(stderr, "Error: IP address is required (-i <ip_address>)\n");
        ctx->exit_code = EXIT_FAILURE;
        usage(ctx);
    }

    if(ctx->port_str == NULL)
    {
        fprintf(stderr, "Error: Port number is required (-p <port>)\n");
        ctx->exit_code = EXIT_FAILURE;
        usage(ctx);
    }

    ctx->port = parse_in_port_t(ctx);
}

static in_port_t parse_in_port_t(client_context *ctx)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(ctx->port_str, &endptr, BASE_TEN);

    if(ctx->port_str == endptr)
    {
        ctx->exit_message = "Error: Port must be a numeric value.\n";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    if(errno != 0)
    {
        ctx->exit_message = "Error: System error parsing port number.\n";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    if(*endptr != '\0')
    {
        fprintf(stderr, "Error: Invalid characters in port input");
        ctx->exit_code = EXIT_FAILURE;
        usage(ctx);
    }

    if(parsed_value < LOWEST_PORT_POSSIBLE || parsed_value > HIGHEST_PORT_POSSIBLE)
    {
        ctx->exit_message = "Error: Port out of range (1024-65535).\n";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    return (in_port_t)parsed_value;
}

_Noreturn static void usage(const client_context *ctx)
{
    fprintf(stderr, "Usage: %s -p <port> -i <ip_address> [-h <help>]\n", ctx->argv[0]);
    fputs("\nOptions:\n,", stderr);
    fputs(" -p <port> Port number of server (required)\n", stderr);
    fputs(" -i <ip_address> IP address of server (required)\n", stderr);
    fputs(" -h <help> Display this help and exit\n", stderr);
    quit(ctx);
}

_Noreturn static void quit(const client_context *ctx)
{
    if(ctx->exit_message != NULL)
    {
        fputs(ctx->exit_message, stderr);
    }
    exit(ctx->exit_code);
}

static void convert_address(client_context *ctx)
{
    memset(&ctx->addr, 0, sizeof(ctx->addr));

    if(inet_pton(AF_INET, ctx->ip_address, &(((struct sockaddr_in *)&ctx->addr)->sin_addr)) == 1)
    {
        ctx->addr.ss_family = AF_INET;
    }
    else
    {
        static char error_buf[ERROR_MESSAGE_BUFFER];
        snprintf(error_buf, sizeof(error_buf), "%s is not an IPv4 address", ctx->ip_address);

        ctx->exit_message = error_buf;
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }
}

static int socket_create() {
    ctx->sockfd = socket(ctx->addr.ss_family, SOCK_STREAM, 0);
    
}

// static void socket_connect() {

// }

// static void shutdown_socket() {

// }

// static void socket_close() {

// }