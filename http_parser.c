#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include "server.h"
#include "http_parser.h"
#include "http_response_builder.h"

#define MAX_REQUEST_SIZE 32768

static int handle_body(client_context* ctx, char* end) {
    long body_size = 0;

    for (int i = 0; i < ctx->header_count; i++) {
        if (ctx->req.headers[i].n_len == 14 &&
            strncasecmp(ctx->req.headers[i].name, "Content-Length", 14) == 0) {
            char tmp[32] = {0};
            size_t len = (size_t)ctx->req.headers[i].v_len < sizeof(tmp) - 1
                         ? (size_t)ctx->req.headers[i].v_len : sizeof(tmp) - 1;
            memcpy(tmp, ctx->req.headers[i].value, len);
            body_size = strtol(tmp, NULL, 10);
            printf("body size: %ld\n", body_size);
            break;
        }
    }

    if (body_size < 0 || body_size + ctx->bytes_parsed >= MAX_REQUEST_SIZE) {
        return -1;
    }

    if (body_size == 0) {
        ctx->req.body = NULL;
        ctx->req.b_len = 0;
        ctx->current_state = DONE;
        return 0;
    }

    size_t avail = end - ctx->curr;
    if (avail < (size_t)body_size) {
        return 0;
    }

    ctx->req.body = ctx->curr;
    ctx->req.b_len = body_size;
    printf("body: %.*s\n", (int)ctx->req.b_len, ctx->req.body);
    ctx->curr += body_size;
    ctx->bytes_parsed += body_size;
    ctx->current_state = DONE;
    return 0;
}

int parse_request(client_context* ctx) {
    char* end = ctx->start + ctx->bytes_to_read;

    if (ctx->current_state == BODY) {
        return handle_body(ctx, end);
    }

    while (ctx->curr < end) {
        if (ctx->bytes_parsed >= MAX_REQUEST_SIZE) {
            return -1;
        }

        size_t remaining = end - ctx->curr;

        switch (ctx->current_state) {
            case METHOD: {
                char* p = memchr(ctx->curr, ' ', remaining);
                if (p != NULL) {
                    ctx->req.method = ctx->curr;
                    ctx->req.m_len = p - ctx->curr;
                    ctx->current_state = URI;
                    ctx->bytes_parsed += ctx->req.m_len + 1;
                    ctx->curr = p + 1;
                    printf("method: %.*s\n", (int)ctx->req.m_len, ctx->req.method);
                } else {
                    return 0;
                }
                break;
            }

            case URI: {
                char* p = memchr(ctx->curr, ' ', remaining);
                if (p != NULL) {
                    ctx->req.uri = ctx->curr;
                    ctx->req.uri_len = p - ctx->curr;
                    ctx->current_state = PROTOCOL;
                    ctx->bytes_parsed += ctx->req.uri_len + 1;
                    ctx->curr = p + 1;
                    printf("uri: %.*s\n", (int)ctx->req.uri_len, ctx->req.uri);
                } else {
                    return 0;
                }
                break;
            }

            case PROTOCOL: {
                char* p = memmem(ctx->curr, remaining, "\r\n", 2);
                if (p != NULL) {
                    ctx->req.protocol = ctx->curr;
                    ctx->req.p_len = p - ctx->curr;
                    ctx->current_state = HEADER_NAME;
                    ctx->bytes_parsed += ctx->req.p_len + 2;
                    ctx->curr = p + 2;
                    printf("protocol: %.*s\n", (int)ctx->req.p_len, ctx->req.protocol);
                } else {
                    return 0;
                }
                break;
            }

            case HEADER_NAME: {
                if (remaining >= 2 && ctx->curr[0] == '\r' && ctx->curr[1] == '\n') {
                    ctx->bytes_parsed += 2;
                    ctx->curr += 2;
                    ctx->current_state = BODY;
                    return handle_body(ctx, end);
                }

                char* line_end = memmem(ctx->curr, remaining, "\r\n", 2);
                if (line_end == NULL) {
                    return 0;
                }

                if (ctx->header_count >= MAX_HEADERS) {
                    return -2;
                }

                char* colon = memmem(ctx->curr, line_end - ctx->curr, ": ", 2);
                if (colon != NULL) {
                    ctx->req.headers[ctx->header_count].name = ctx->curr;
                    ctx->req.headers[ctx->header_count].n_len = colon - ctx->curr;
                    ctx->current_state = HEADER_VALUE;
                    ctx->bytes_parsed += ctx->req.headers[ctx->header_count].n_len + 2;
                    ctx->curr = colon + 2;
                    printf("header name: %.*s\n",
                           (int)ctx->req.headers[ctx->header_count].n_len,
                           ctx->req.headers[ctx->header_count].name);
                } else {
                    return -2;
                }
                break;
            }

            case HEADER_VALUE: {
                char* p = memmem(ctx->curr, remaining, "\r\n", 2);
                if (p != NULL) {
                    ctx->req.headers[ctx->header_count].value = ctx->curr;
                    ctx->req.headers[ctx->header_count].v_len = p - ctx->curr;
                    ctx->current_state = HEADER_NAME;
                    ctx->bytes_parsed += ctx->req.headers[ctx->header_count].v_len + 2;
                    ctx->curr = p + 2;
                    printf("header value: %.*s\n",
                           (int)ctx->req.headers[ctx->header_count].v_len,
                           ctx->req.headers[ctx->header_count].value);
                    ctx->header_count++;
                } else {
                    return 0;
                }
                break;
            }

            case BODY:
                printf("qui");
                return handle_body(ctx, end);

            case DONE:
                return 0;
        }
    }

    return 0;
}


bool is_uri_safe(http_request req) {
    if (req.uri == NULL || req.uri_len <= 0 || req.uri[0] != '/') {
        return false;
    }
    if (memmem(req.uri, req.uri_len, "..", 2) != NULL ||
        memmem(req.uri, req.uri_len, "\\", 1) != NULL) {
        return false;
    }
    return true;
}

int check_request(http_request req) {
    if (req.p_len != 8 || strncmp(req.protocol, "HTTP/1.", 7) != 0 ||
        (req.protocol[7] != '0' && req.protocol[7] != '1')) {
        return 400;
    }

    if (!is_uri_safe(req)) {
        return 400;
    }

    char filepath[256];
    if (get_file_path(req.uri, req.uri_len, filepath, sizeof(filepath)) < 0) {
        return 400;
    }

    if (req.m_len == 3 && strncmp(req.method, "GET", 3) == 0) {
        printf("path: %s",filepath);
        if (access(filepath, F_OK) != 0) return 404;
        return 200;
    }
    else if (req.m_len == 4 && strncmp(req.method, "POST", 4) == 0) {
        if (access(filepath, F_OK) != 0) return 201;
        return 200;
    }
    else if (req.m_len == 3 && strncmp(req.method, "PUT", 3) == 0) {
        if (access(filepath, F_OK) != 0) return 201;
        return 409;
    }
    else if (req.m_len == 6 && strncmp(req.method, "DELETE", 6) == 0) {
        return 204;
    }
    else {
        return 405;
    }
}
