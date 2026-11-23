#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include "http_parser.h"
#include "http_response_builder.h"

#define BUFFERSIZE 50000

typedef struct client_context_struct {
    int fd;
    char buffer[BUFFERSIZE];
    size_t total_bytes_read;
    size_t bytes_to_read;
    size_t bytes_parsed;
    char* start;
    char* curr;
    http_request req;
    http_response res;
    int header_count;
    enum STATE current_state;
} client_context;

#endif
