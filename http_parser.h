#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stdbool.h>

#define MAX_HEADERS 128

typedef struct client_context_struct client_context;

typedef struct http_headers_struct {
    char *name;
    int n_len;
    char *value;
    int v_len;
} http_headers;

typedef struct http_request_struct {
    char *method;
    int m_len;
    char *uri;
    int uri_len;
    char *protocol;
    int p_len;
    http_headers headers[MAX_HEADERS];
    char *body;
    int b_len;
} http_request;

enum STATE {
    METHOD,
    URI,
    PROTOCOL,
    HEADER_NAME,
    HEADER_VALUE,
    BODY,
    DONE
};

int parse_request(client_context* ctx);
int check_request(http_request req);
bool is_uri_safe(http_request req);

#endif
