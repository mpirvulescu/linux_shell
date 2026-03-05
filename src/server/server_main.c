#include "server/server_main.h"
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

static void           parse_arguments(server_context *ctx);
static void           handle_arguments(server_context *ctx);
static server_context init_context(void);
// this is just used by handle_arguments
static in_port_t      parse_in_port_t(server_context *ctx);
_Noreturn static void usage(const server_context *ctx);
_Noreturn static void quit(const server_context *ctx);
static void           initialize_server(server_context *ctx);
static void           accept_client(server_context *ctx);
static void           read_input(server_context *ctx);
static void           analyze_command(server_context *ctx);
static void           run_builtin(server_context *ctx);
static int            fork_process(void);
static void           run_external(server_context *ctx);
static void           wait_for_child(pid_t pid);
static void           close_session(server_context *ctx);
static void           close_server(server_context *ctx);

int main(const int argc, char **argv)
{
    server_context ctx;
    ctx      = init_context();
    ctx.argc = argc;
    ctx.argv = argv;

    parse_arguments(&ctx);
    handle_arguments(&ctx);
    initialize_server(&ctx);
    
    while(1)
    {
        accept_client(&ctx);
    }

    close_server(&ctx);
    return 0;
}

static server_context init_context(void)
{
    server_context ctx = {0};
    ctx.argc           = 0;
    ctx.argv           = NULL;
    ctx.exit_code      = EXIT_SUCCESS;
    ctx.exit_message   = NULL;
    ctx.sockfd         = -1;
    return ctx;
}

static void parse_arguments(server_context *ctx)
{
    int         opt;
    const char *optstring = ":p:h";
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

static void handle_arguments(server_context *ctx)
{
    if(ctx->port_str == NULL)
    {
        fprintf(stderr, "Error: Port number is required (-p <port>)\n");
        ctx->exit_code = EXIT_FAILURE;
        usage(ctx);
    }

    ctx->port = parse_in_port_t(ctx);
}

static in_port_t parse_in_port_t(server_context *ctx)
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

_Noreturn static void usage(const server_context *ctx)
{
    fprintf(stderr, "Usage: %s -p <port> [-h <help>]\n", ctx->argv[0]);
    fputs("\nOptions:\n,", stderr);
    fputs(" -p <port> Port number of server (required)\n", stderr);
    fputs(" -h <help> Display this help and exit\n", stderr);
    quit(ctx);
}

_Noreturn static void quit(const server_context *ctx)
{
    if(ctx->exit_message != NULL)
    {
        fputs(ctx->exit_message, stderr);
    }
    exit(ctx->exit_code);
}

static void initialize_server(server_context *ctx)
{
    struct sockaddr_in serv_addr;
    socklen_t addr_len = sizeof(serv_addr);

    // Create socket
    ctx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(ctx->sockfd == -1)
    {
        ctx->exit_message = "Socket creation failed";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    // Set up server address structure
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(ctx->port);

    // Bind socket
    if(bind(ctx->sockfd, (struct sockaddr *)&serv_addr, addr_len) == -1)
    {
        ctx->exit_message = "Bind failed";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    // Listen for connections
    if(listen(ctx->sockfd, 5) == -1)
    {
        ctx->exit_message = "Listen failed";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    printf("Server listening on port %u\n", ctx->port);
}

static void accept_client(server_context *ctx)
{
    int client_fd;
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Accept connection
    client_fd = accept(ctx->sockfd, (struct sockaddr *)&client_addr, &addr_len);
    if(client_fd == -1)
    {
        perror("Accept failed");
        return;
    }

    printf("Client connected\n");

    // Fork to handle client
    pid_t pid = fork();
    if(pid == 0)
    {
        // Child process
        close(ctx->sockfd); // Close listening socket in child
        
        while(1)
        {
            read_input(ctx);
            
            if(ctx->bytes_received <= 0)
            {
                break;
            }
            
            analyze_command(ctx);
        }
        
        close_session(ctx);
        exit(EXIT_SUCCESS);
    }
    else if(pid > 0)
    {
        // Parent process
        close(client_fd); // Close client socket in parent
        waitpid(pid, NULL, 0); // Wait for child to complete
    }
    else
    {
        perror("Fork failed");
        close(client_fd);
    }
}

static void read_input(server_context *ctx)
{
    memset(ctx->user_input, 0, sizeof(ctx->user_input));
    
    ctx->bytes_received = recv(ctx->sockfd, ctx->user_input, sizeof(ctx->user_input) - 1, 0);
    
    if(ctx->bytes_received == 0)
    {
        // Client disconnected
        return;
    }
    
    if(ctx->bytes_received == -1)
    {
        perror("Read failed");
        return;
    }
    
    ctx->user_input[ctx->bytes_received] = '\0';
}

static void analyze_command(server_context *ctx)
{
    char *cmd_copy = strdup(ctx->user_input);
    if(cmd_copy == NULL)
    {
        // Handle error
        return;
    }

    // Check for built-in commands first
    if(strcmp(ctx->user_input, "exit") == 0)
    {
        run_builtin(ctx);
        free(cmd_copy);
        return;
    }
    
    if(strncmp(ctx->user_input, "cd ", 3) == 0)
    {
        run_builtin(ctx);
        free(cmd_copy);
        return;
    }

    // External command
    fork_process();
    run_external(ctx);
    free(cmd_copy);
}

static void run_builtin(server_context *ctx)
{
    if(strcmp(ctx->user_input, "exit") == 0)
    {
        // Exit command - just return to close session
        return;
    }
    
    if(strncmp(ctx->user_input, "cd ", 3) == 0)
    {
        char *dir = ctx->user_input + 3;
        while(*dir == ' ') dir++; // Skip leading spaces
        
        if(chdir(dir) != 0)
        {
            perror("cd failed");
        }
    }
}

static int fork_process(void)
{
    pid_t pid = fork();
    if(pid == -1)
    {
        perror("Fork failed");
        return -1;
    }
    return pid;
}

static void run_external(server_context *ctx)
{
    // This is a simplified implementation
    // In a full implementation, you would parse the command and arguments
    // and handle redirection operators (<, >, 2>)
    
    char *args[32];
    int arg_count = 0;
    
    // Simple parsing - in reality this would be more complex
    char *token = strtok(ctx->user_input, " ");
    while(token != NULL && arg_count < 31)
    {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;
    
    if(arg_count > 0)
    {
        execvp(args[0], args);
        // If execvp returns, it means it failed
        perror("Command execution failed");
        exit(EXIT_FAILURE);
    }
}

static void wait_for_child(pid_t pid)
{
    int status;
    waitpid(pid, &status, 0);
}

static void close_session(server_context *ctx)
{
    if(ctx->sockfd != -1)
    {
        close(ctx->sockfd);
        ctx->sockfd = -1;
    }
}

static void close_server(server_context *ctx)
{
    if(ctx->sockfd != -1)
    {
        close(ctx->sockfd);
        ctx->sockfd = -1;
    }
}
