
#include "../include/utils.h"
#include "../include/main.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int convert_address(server_context *ctx)
{
    memset(&ctx->addr, 0, sizeof(ctx->addr));

    // IPv4
    if(inet_pton(AF_INET, ctx->ip_address, &(((struct sockaddr_in *)&ctx->addr)->sin_addr)) == 1)
    {
        ctx->addr.ss_family = AF_INET;
        return 0;
    }

    // IPv6
    if(inet_pton(AF_INET6, ctx->ip_address, &(((struct sockaddr_in6 *)&ctx->addr)->sin6_addr)) == 1)
    {
        ctx->addr.ss_family = AF_INET6;
        return 0;
    }

    return -1;
}

int get_tokens_in_str(const char *string, const char *delimiter)
{
    char       *temp_string;
    const char *token;
    char       *save_ptr;

    int count = 0;

    temp_string = strdup(string);
    if(temp_string == NULL)
    {
        perror("Memory allocation failed when getting tokens in string\n");
        return -1;
    }
    token = strtok_r(temp_string, delimiter, &save_ptr);
    while(token != NULL)
    {
        count++;
        token = strtok_r(NULL, delimiter, &save_ptr);
    }
    free(temp_string);
    return count;
}

struct split_string str_split(const char *string, const char *delimiter)
{
    const char         *token;
    struct split_string result = {0};
    char               *temp_string;
    char               *token_dup;
    int                 tokens_count;
    char               *save_ptr;

    if(delimiter == NULL || delimiter[0] == '\0' || string == NULL || string[0] == '\0')
    {
        result.strings = NULL;
        return result;
    }

    tokens_count = get_tokens_in_str(string, delimiter);
    if(tokens_count <= 0)
    {
        result.strings = NULL;
        return result;
    }

    result.count   = tokens_count;
    result.strings = (char **)malloc(sizeof(char *) * result.count);
    if(result.strings == NULL)
    {
        return result;
    }
    memset((void *)result.strings, 0, (sizeof(char *) * result.count));

    temp_string = strdup(string);
    if(temp_string == NULL)
    {
        perror("Memory allocation failed when getting tokens in string\n");
        free((void *)result.strings);
        result.strings = NULL;
        return result;
    }
    token = strtok_r(temp_string, delimiter, &save_ptr);
    for(size_t i = 0; i < result.count; i++)
    {
        if(token == NULL)
        {
            fputs("str_split error: less tokens than expected\n", stderr);
            free(temp_string);
            free_split_string(&result);
            result.strings = NULL;
            return result;
        }
        token_dup = strdup(token);
        if(token_dup == NULL)
        {
            perror("Memory allocation failed when splitting strings\n");
            free(temp_string);
            free_split_string(&result);
            result.strings = NULL;
            return result;
        }
        result.strings[i] = token_dup;

        token = strtok_r(NULL, delimiter, &save_ptr);
    }
    free(temp_string);

    return result;
}

void free_split_string(struct split_string *split_string)
{
    if(split_string->strings == NULL)
    {
        return;
    }

    for(int i = 0; i < split_string->count; i++)
    {
        if(split_string->strings[i] == NULL)
        {
            continue;
        }
        free(split_string->strings[i]);
        split_string->strings[i] = NULL;
    }
    free((void *)split_string->strings);
    split_string->strings = NULL;
}

ssize_t read_full(int fd, void *buf, size_t n)
{
    unsigned char *p          = (unsigned char *)buf;
    size_t         bytes_left = n;

    while(bytes_left > 0)
    {
        ssize_t bytes_read = read(fd, p, bytes_left);

        if(bytes_read == 0)
        {
            return RW_EOF;
        }

        if(bytes_read == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return RW_ERROR;
        }

        p += bytes_read;
        bytes_left -= (size_t)bytes_read;
    }

    return RW_SUCCESS;
}

ssize_t write_full(int fd, const void *buf, size_t n)
{
    const unsigned char *p          = (const unsigned char *)buf;
    size_t               bytes_left = n;

    while(bytes_left > 0)
    {
        ssize_t bytes_written = write(fd, p, bytes_left);

        if(bytes_written == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return RW_ERROR;
        }

        p += bytes_written;
        bytes_left -= (size_t)bytes_written;
    }

    return RW_SUCCESS;
}