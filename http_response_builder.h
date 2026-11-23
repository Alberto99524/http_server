#ifndef HTTP_RESPONSE_BUILDER_H
#define HTTP_RESPONSE_BUILDER_H

#include <stddef.h>
#include "http_parser.h"

typedef struct client_context_struct client_context;

typedef struct http_response_struct {
    char *protocol;
    int p_len;
    int code;
    int c_len;
    char *description;
    int d_len;
    http_headers headers[MAX_HEADERS];
    char *body;
    int b_len;
} http_response;

char* build_response(client_context* ctx, size_t* out_len);

char* build_ok(client_context* ctx, size_t* out_len);
char* build_created(client_context* ctx, size_t* out_len);
char* build_no_content(client_context* ctx, size_t* out_len);
char* build_bad_request(size_t* out_len);
char* build_not_found(size_t* out_len);
char* build_method_not_supported(size_t* out_len);
char* build_conflict(size_t* out_len);
char* build_request_entity_too_large(size_t* out_len);
char* build_request_header_fields_too_large(size_t* out_len);

char* get_file_content(const char* filePath, size_t* out_size);
int get_file_path(const char* uri, int uri_len, char* out_path, size_t out_size);
int write_file(const char* filepath, const char* content, size_t content_len);
int delete_file(const char* filepath);
const char* get_content_type(const char* filepath, int filepath_len);

#endif
