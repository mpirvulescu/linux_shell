#ifndef NETWORK_H
#define NETWORK_H

#include "main.h"

// Public network functions
void setup_signal_handler(void);
void init_server_socket(server_context *ctx);
void init_poll_fds(server_context *ctx);
void event_loop(server_context *ctx);
void close_client(server_context *ctx, const client_state *state); 
void cleanup_server(const server_context *ctx);

#endif /* NETWORK_H */