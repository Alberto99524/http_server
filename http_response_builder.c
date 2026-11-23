#include "http_response_builder.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILEPATH 256
#define DEFAULT_DOCUMENT_ROOT "data"
#define MAX_PATH_SIZE 1024

char* build_response(client_context* ctx, size_t* out_len) {
    int code = check_request(ctx->req);
    char* response = NULL;

    switch (code) {
        case 200: response = build_ok(ctx, out_len); break;
        case 201: response = build_created(ctx, out_len); break;
        case 204: response = build_no_content(ctx, out_len); break;
        case 400: response = build_bad_request(out_len); break;
        case 404: response = build_not_found(out_len); break;
        case 405: response = build_method_not_supported(out_len); break;
        case 409: response = build_conflict(out_len); break;
        case 413: response = build_request_entity_too_large(out_len); break;
        case 431: response = build_request_header_fields_too_large(out_len); break;
        default:  response = build_bad_request(out_len); break;
    }
    return response;
}

static char* make_static_response(const char* str, size_t* out_len) {
    *out_len = strlen(str);
    return strdup(str);
}

char* build_bad_request(size_t* out_len) {
    return make_static_response("HTTP/1.0 400 Bad Request\r\nContent-Length: 0\r\n\r\n", out_len);
}

char* build_not_found(size_t* out_len) {
    return make_static_response("HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n", out_len);
}

char* build_method_not_supported(size_t* out_len) {
    return make_static_response("HTTP/1.0 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n", out_len);
}

char* build_conflict(size_t* out_len) {
    return make_static_response("HTTP/1.0 409 Conflict\r\nContent-Length: 0\r\n\r\n", out_len);
}

char* build_request_entity_too_large(size_t* out_len) {
    return make_static_response("HTTP/1.0 413 Request Entity Too Large\r\nContent-Length: 0\r\n\r\n", out_len);
}

char* build_request_header_fields_too_large(size_t* out_len) {
    return make_static_response("HTTP/1.0 431 Request Header Fields Too Large\r\nContent-Length: 0\r\n\r\n", out_len);
}

char* build_no_content(client_context* ctx, size_t* out_len) {
    char filepath[MAX_FILEPATH];
    if (get_file_path(ctx->req.uri, ctx->req.uri_len, filepath, sizeof(filepath)) == 0) {
        delete_file(filepath);
    }
    return make_static_response("HTTP/1.0 204 No Content\r\n\r\n", out_len);
}

char* build_ok(client_context* ctx, size_t* out_len) {
    if (ctx->req.m_len == 3 && strncmp(ctx->req.method, "GET", 3) == 0) {
        char filepath[MAX_FILEPATH];
        if (get_file_path(ctx->req.uri, ctx->req.uri_len, filepath, sizeof(filepath)) < 0) {
            return build_bad_request(out_len);
        }

        size_t body_len = 0;
        char* body = get_file_content(filepath, &body_len);
        if (body == NULL) {
            return build_not_found(out_len);
        }

        const char* content_type = get_content_type(ctx->req.uri, ctx->req.uri_len);
        if (content_type == NULL) {
            content_type = "Content-Type: application/octet-stream";
        }

        char header[512];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.0 200 OK\r\n"
            "%s\r\n"
            "Content-Length: %zu\r\n\r\n",
            content_type, body_len);

        char* response = malloc(header_len + body_len);
        if (response == NULL) {
            free(body);
            return NULL;
        }

        memcpy(response, header, header_len);
        memcpy(response + header_len, body, body_len);
        free(body);

        *out_len = header_len + body_len;
        return response;
    }

    if (ctx->req.m_len == 4 && strncmp(ctx->req.method, "POST", 4) == 0) {
        char filepath[MAX_FILEPATH];
        if (get_file_path(ctx->req.uri, ctx->req.uri_len, filepath, sizeof(filepath)) < 0) {
            return build_bad_request(out_len);
        }
        write_file(filepath, ctx->req.body, ctx->req.b_len);
        return make_static_response(
            "HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: 17\r\n\r\n{\"success\": true}",
            out_len);
    }

    return build_method_not_supported(out_len);
}

char* build_created(client_context* ctx, size_t* out_len) {
    char filepath[MAX_FILEPATH];
    if (get_file_path(ctx->req.uri, ctx->req.uri_len, filepath, sizeof(filepath)) < 0) {
        return build_bad_request(out_len);
    }
    write_file(filepath, ctx->req.body, ctx->req.b_len);
    return make_static_response(
        "HTTP/1.0 201 Created\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: 17\r\n\r\n{\"success\": true}",
        out_len);
}

char* get_file_content(const char* filePath, size_t* out_size) {
    if (filePath == NULL) return NULL;
    FILE* f = fopen(filePath, "rb");
    if (f == NULL) return NULL;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char* buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, file_size, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    *out_size = read_bytes;
    return buffer;
}

int get_file_path(const char* uri, int uri_len, char* out_path, size_t out_size) {
    if (uri == NULL || uri_len <= 0 || uri_len >= MAX_PATH_SIZE) return -1;

    char clean_uri[MAX_PATH_SIZE];
    memcpy(clean_uri, uri, uri_len);
    clean_uri[uri_len] = '\0';

    if (strstr(clean_uri, "..") != NULL) return -1;

    int written = snprintf(out_path, out_size, "%s%s", DEFAULT_DOCUMENT_ROOT, clean_uri);
    if (written < 0 || (size_t)written >= out_size) return -1;

    return 0;
}

int write_file(const char* filepath, const char* content, size_t content_len) {
    if (filepath == NULL) return -1;
    FILE* file = fopen(filepath, "wb");
    if (file == NULL) return -1;

    if (content != NULL && content_len > 0) {
        fwrite(content, 1, content_len, file);
    }
    fclose(file);
    return 0;
}

int delete_file(const char* filepath) {
    if (filepath == NULL) return -1;
    return remove(filepath);
}

const char* get_content_type(const char* filepath, int filepath_len) {
    if (filepath == NULL || filepath_len <= 0) return NULL;

    static const struct {
        const char* ext;
        const char* mime;
    } mime_types[] = {
        {".pdf",  "Content-Type: application/pdf"},
        {".json", "Content-Type: application/json"},
        {".xml",  "Content-Type: application/xml"},
        {".zip",  "Content-Type: application/zip"},
        {".gif",  "Content-Type: image/gif"},
        {".jpeg", "Content-Type: image/jpeg"},
        {".jpg",  "Content-Type: image/jpeg"},
        {".png",  "Content-Type: image/png"},
        {".css",  "Content-Type: text/css; charset=utf-8"},
        {".csv",  "Content-Type: text/csv; charset=utf-8"},
        {".html", "Content-Type: text/html; charset=utf-8"},
        {".htm",  "Content-Type: text/html; charset=utf-8"},
        {".js",   "Content-Type: text/javascript; charset=utf-8"},
        {".txt",  "Content-Type: text/plain; charset=utf-8"}
    };

    size_t num_types = sizeof(mime_types) / sizeof(mime_types[0]);
    for (size_t i = 0; i < num_types; i++) {
        size_t ext_len = strlen(mime_types[i].ext);
        if ((size_t)filepath_len >= ext_len &&
            strncmp(filepath + filepath_len - ext_len, mime_types[i].ext, ext_len) == 0) {
            return mime_types[i].mime;
        }
    }
    return NULL;
}
