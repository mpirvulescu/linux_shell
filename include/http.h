#ifndef HTTP_H
#define HTTP_H

#include "main.h"

void read_request(server_context *ctx, client_state *state);
void send_error_response(server_context *ctx, client_state *state, int status_code);

#endif /* HTTP_H */