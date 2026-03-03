#ifndef UTILS_H
#define UTILS_H

#include "main.h"
#include <sys/types.h> 

int convert_address(server_context *ctx);
int get_tokens_in_str(const char *string, const char *delimiter);
struct split_string str_split(const char *string, const char *delimiter);
void free_split_string(struct split_string *split_string);
ssize_t read_full(int fd, void *buf, size_t n);
ssize_t write_full(int fd, const void *buf, size_t n);

#endif /* UTILS_H */