#include "server/server_main.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void           sigchild_handler(int sig);
static void           setup_sigchld(void);
static void           parse_arguments(server_context *ctx);
static void           handle_arguments(server_context *ctx);
static server_context init_context(void);
// this is just used by handle_arguments
static in_port_t      parse_in_port_t(server_context *ctx);
_Noreturn static void usage(const server_context *ctx);
_Noreturn static void quit(const server_context *ctx);
static void           initialize_server(server_context *ctx);
static void           spawn_worker(server_context *ctx, size_t index);
static void           replace_workers(server_context *ctx);
static void           create_children_pool(server_context *ctx);
static void           accept_client(server_context *ctx);
static void           read_input(server_context *ctx);
static void           analyze_command(server_context *ctx);
static int            parse_command(char *cmd, char *argv[], int argv_max, char **in_file, char **out_file, char **err_file);
static char          *trim(char *s);
static char          *find_path(const char *command);
static void           run_builtin(server_context *ctx);
static int            fork_process(void);
static void           run_external(server_context *ctx);
static void           wait_for_child(pid_t pid);
static void           close_session(server_context *ctx);
static void           close_server(server_context *ctx);

static volatile sig_atomic_t child_died = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void handle_sigint(int sig)
{
    (void)sig;
    const char *msg = "\nShutting down worker pool...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    // Kill the entire process group so children die with the parent
    kill(0, SIGTERM);
    _exit(EXIT_SUCCESS);
}

static void sigchild_handler(int sig)
{
    (void)sig;
    child_died = 1;
}

static void setup_sigchld(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchild_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;    // Restart accept() if interrupted
    sigaction(SIGCHLD, &sa, NULL);
}

int main(const int argc, char **argv)
{
    server_context ctx;
    ctx      = init_context();
    ctx.argc = argc;
    ctx.argv = argv;

    parse_arguments(&ctx);
    handle_arguments(&ctx);
    signal(SIGINT, handle_sigint);
    initialize_server(&ctx);
    create_children_pool(&ctx);

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
    ctx.clientfd       = -1;
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
    socklen_t          addr_len = sizeof(serv_addr);
    int                opt      = 1;

    // Create socket
    ctx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(ctx->sockfd == -1)
    {
        ctx->exit_message = "Socket creation failed";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    // aalow restart of server
    if(setsockopt(ctx->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt with SO_REUSEADDR failed");
    }

    // Set up server address structure
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(ctx->port);

    // Bind socket
    if(bind(ctx->sockfd, (struct sockaddr *)&serv_addr, addr_len) == -1)
    {
        ctx->exit_message = "Bind failed";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    // Listen for connections
    if(listen(ctx->sockfd, MAX_CONNECTIONS) == -1)
    {
        ctx->exit_message = "Listen failed";
        ctx->exit_code    = EXIT_FAILURE;
        quit(ctx);
    }

    printf("Server listening on port %u\n", ctx->port);
}

static void spawn_worker(server_context *ctx, size_t index)
{
    pid_t pid = fork();
    if(pid == 0)
    {
        // Child Logic
        while(1)
        {
            accept_client(ctx);
            if(ctx->clientfd != -1)
            {
                while(1)
                {
                    read_input(ctx);
                    if(ctx->bytes_received <= 0)
                    {
                        break;
                    }
                    analyze_command(ctx);
                    send(ctx->clientfd, "", 1, 0);
                }
                close_session(ctx);
            }
        }
        exit(EXIT_SUCCESS);
    }
    else
    {
        ctx->worker_pids[index] = pid;
    }
}

static void replace_workers(server_context *ctx)
{
    pid_t dead;
    int   status;

    // Reap all dead children without blocking
    while((dead = waitpid(-1, &status, WNOHANG)) > 0)
    {
        for(size_t i = 0; i < MAX_NUM_CHILDREN; i++)
        {
            if(ctx->worker_pids[i] == dead)
            {
                printf("Worker %d died. Replacing...\n", dead);
                spawn_worker(ctx, i);
                break;
            }
        }
    }
}

static void create_children_pool(server_context *ctx)
{
    setup_sigchld();

    // Initial pool creation
    for(size_t i = 0; i < MAX_NUM_CHILDREN; i++)
    {
        spawn_worker(ctx, i);
    }

    // Parent management loop
    while(1)
    {
        if(child_died)
        {
            child_died = 0;
            replace_workers(ctx);    //
        }
        pause();    // Sleep until the next signal arrives
    }
}

static void accept_client(server_context *ctx)
{
    int                     client_fd;
    struct sockaddr_storage client_addr;
    socklen_t               addr_len = sizeof(client_addr);

    // Accept connection
    client_fd = accept(ctx->sockfd, (struct sockaddr *)&client_addr, &addr_len);
    if(client_fd == -1)
    {
        perror("Accept failed");
        return;
    }

    ctx->clientfd = client_fd;

    printf("Client connected\n");
}

static void read_input(server_context *ctx)
{
    memset(ctx->user_input, 0, sizeof(ctx->user_input));

    ctx->bytes_received = recv(ctx->clientfd, ctx->user_input, sizeof(ctx->user_input) - 1, 0);

    if(ctx->bytes_received == 0)
    {
        // Client disconnected
        printf("Client disconnected");
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
    // Check for built-in 'exit'
    if(strcmp(ctx->user_input, "exit") == 0)
    {
        run_builtin(ctx);
        return;    // Prevent fall-through to run_external
    }

    // Check for built-in 'cd'
    if(strncmp(ctx->user_input, "cd ", 3) == 0 || strcmp(ctx->user_input, "cd") == 0)
    {
        run_builtin(ctx);
        return;    // Prevent fall-through to run_external
    }

    // External command: Remove the fork_process() call here.
    // run_external handles its own fork.
    run_external(ctx);
}

static void run_builtin(server_context *ctx)
{
    if(strcmp(ctx->user_input, "exit") == 0)
    {
        // Exit command - just return to close session
        return;
    }

    if(strncmp(ctx->user_input, "cd ", 2) == 0)
    {
        const char *dir = ctx->user_input + 2;
        while(*dir == ' ')
        {
            dir++;
        }
        if(*dir == '\0')
        {
            dir = getenv("HOME");    // defauklt to home if no path given
        }

        if(dir == NULL)
        {
            const char *err = "cd: HOME not set\n";
            write(ctx->clientfd, err, strlen(err));
            return;
        }

        // Skip leading spaces

        if(chdir(dir) != 0)
        {
            perror("cd failed");
        }
        return;
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
    char  cmd_copy[USER_MESSAGE_BUFFER];
    char *argv[MAX_ARGS];
    char *in_file  = NULL;
    char *out_file = NULL;
    char *err_file = NULL;
    int   argc;
    pid_t pid;
    int   status;

    // working on copy so i dont destroy user input
    strncpy(cmd_copy, ctx->user_input, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    argc = parse_command(trim(cmd_copy), argv, MAX_ARGS, &in_file, &out_file, &err_file);

    if(argc == 0)
    {
        return;    // nothing found in command
    }

    // resolve executable path
    char *path = find_path(argv[0]);
    if(path == NULL)
    {
        char msg[ERROR_MESSAGE_BUFFER];
        int  n = snprintf(msg, sizeof(msg), "%s: command not found\n", argv[0]);
        send(ctx->clientfd, msg, (size_t)n, 0);
        return;
    }

    // replace first argument with full path
    argv[0] = path;

    pid = fork();
    if(pid == -1)
    {
        perror("fork");
        return;
    }

    if(pid == 0)
    {
        // grandchild that redirects the fds, then execs

        // handling stdin redirection  (<)
        if(in_file != NULL)
        {
            int fd = open(in_file, O_RDONLY | O_CLOEXEC);
            if(fd == -1)
            {
                char msg[ERROR_MESSAGE_BUFFER];
                snprintf(msg, sizeof(msg), "open %s: %s\n", in_file, strerror(errno));
                write(ctx->clientfd, msg, strlen(msg));
                exit(EXIT_FAILURE);
            }
            if(dup2(fd, STDIN_FILENO) == -1)
            {
                perror("dup2 stdin");
                exit(EXIT_FAILURE);
            }
            close(fd);
        }

        // handling stdout redirection (>), if not redirected, send to client
        if(out_file != NULL)
        {
            int fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, FILE_PERMISSIONS);
            if(fd == -1)
            {
                char msg[ERROR_MESSAGE_BUFFER];
                snprintf(msg, sizeof(msg), "open %s: %s\n", out_file, strerror(errno));
                write(ctx->clientfd, msg, strlen(msg));
                exit(EXIT_FAILURE);
            }
            if(dup2(fd, STDOUT_FILENO) == -1)
            {
                perror("dup2 stdout");
                exit(EXIT_FAILURE);
            }
            close(fd);
        }
        else
        {
            // handling no file redirect: sending stdout back over the socket
            if(dup2(ctx->clientfd, STDOUT_FILENO) == -1)
            {
                perror("dup2 stdout→socket");
                exit(EXIT_FAILURE);
            }
        }

        // handling stderr redirection (2>)
        if(err_file != NULL)
        {
            int fd = open(err_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, FILE_PERMISSIONS);
            if(fd == -1)
            {
                char msg[ERROR_MESSAGE_BUFFER];
                snprintf(msg, sizeof(msg), "open %s: %s\n", err_file, strerror(errno));
                write(ctx->clientfd, msg, strlen(msg));
                exit(EXIT_FAILURE);
            }
            if(dup2(fd, STDERR_FILENO) == -1)
            {
                perror("dup2 stderr");
                exit(EXIT_FAILURE);
            }
            close(fd);
        }
        else
        {
            // handling no file redirect: send stderr back over the socket too
            if(dup2(ctx->clientfd, STDERR_FILENO) == -1)
            {
                perror("dup2 stderr→socket");
                exit(EXIT_FAILURE);
            }
        }

        //  grandchild no longer needs listening socket
        close(ctx->sockfd);

        execv(argv[0], argv);
        // execv only returns on error//
        {
            char msg[ERROR_MESSAGE_BUFFER];
            snprintf(msg, sizeof(msg), "execv %s: %s\n", argv[0], strerror(errno));
            write(STDERR_FILENO, msg, strlen(msg));
        }
        exit(EXIT_FAILURE);
    }

    // wait for grandchild
    if(waitpid(pid, &status, 0) == -1)
    {
        perror("waitpid");
    }
}

static void wait_for_child(pid_t pid)
{
    int status;
    waitpid(pid, &status, 0);
}

static void close_session(server_context *ctx)
{
    if(ctx->clientfd != -1)
    {
        printf("closing client\n");
        close(ctx->clientfd);
        ctx->clientfd = -1;
    }
}

static void close_server(server_context *ctx)
{
    if(ctx->sockfd != -1)
    {
        printf("closing server\n");
        close(ctx->sockfd);
        ctx->sockfd = -1;
    }
}

static char *find_path(const char *command)
{
    static char full_path[PATH_BUFFER];
    const char *search_paths[] = {"/bin/", "/usr/bin/", "/usr/local/bin/", NULL};

    if(command[0] == '/')
    {
        return (char *)command;
    }

    for(int i = 0; search_paths[i] != NULL; i++)
    {
        snprintf(full_path, sizeof(full_path), "%s%s", search_paths[i], command);
        if(access(full_path, X_OK) == 0)
        {
            return full_path;
        }
    }
    return NULL;
}

static char *trim(char *s)
{
    char *end;
    while(*s == ' ' || *s == '\t')
    {
        s++;
    }
    if(*s == '\0')
    {
        return s;
    }
    end = s + strlen(s) - 1;
    while(end > s && (*end == ' ' || *end == '\t'))
    {
        *end-- = '\0';
    }
    return s;
}

static int parse_command(char *cmd, char *argv[], int argv_max, char **in_file, char **out_file, char **err_file)
{
    char *saveptr = NULL;
    char *token;
    int   argc = 0;

    *in_file  = NULL;
    *out_file = NULL;
    *err_file = NULL;

    token = strtok_r(cmd, " \t", &saveptr);
    while(token != NULL && argc < argv_max - 1)
    {
        if(strcmp(token, "<") == 0)
        {
            token = strtok_r(NULL, " \t", &saveptr);
            if(token != NULL)
            {
                *in_file = token;
            }
        }
        else if(strcmp(token, ">") == 0)
        {
            token = strtok_r(NULL, " \t", &saveptr);
            if(token != NULL)
            {
                *out_file = token;
            }
        }
        else if(strcmp(token, "2>") == 0)
        {
            token = strtok_r(NULL, " \t", &saveptr);
            if(token != NULL)
            {
                *err_file = token;
            }
        }
        else if(strncmp(token, "2>", 2) == 0 && token[2] != '\0')
        {
            /* handle "2>file" written without a space */
            *err_file = token + 2;
        }
        else
        {
            argv[argc++] = token;
        }

        token = strtok_r(NULL, " \t", &saveptr);
    }
    argv[argc] = NULL;
    return argc;
}
