#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <fcntl.h>

#include "server.h"
#include "http_parser.h"
#include "http_response_builder.h"

#define MAX_EVENTS 10
#define NUM_THREADS 1

int make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) return -1;
    flags |= O_NONBLOCK;
    return fcntl(sfd, F_SETFL, flags);
}

static void send_all(int fd, const char* buf, size_t len) {
    size_t sent_total = 0;
    while (sent_total < len) {
        ssize_t n = write(fd, buf + sent_total, len - sent_total);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                continue;
            }
            break;
        }
        sent_total += (size_t)n;
    }
}

void* worker_thread_cycle(void* arg) {
    int thread_id = (int)(intptr_t)arg;
    int tcp_socket, epoll_fd;
    struct epoll_event ev, events[MAX_EVENTS];
    int BACKLOG = 128;
    struct sockaddr_in server_address, client_address;

    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == -1) {
        perror("error in creating socket");
        return NULL;
    }

    int opt = 1;
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("SO_REUSEADDR failed");
        close(tcp_socket);
        return NULL;
    }
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("SO_REUSEPORT failed");
        close(tcp_socket);
        return NULL;
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(tcp_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("error in binding socket");
        close(tcp_socket);
        return NULL;
    }

    if (listen(tcp_socket, BACKLOG) == -1) {
        perror("error in listening to a socket");
        close(tcp_socket);
        return NULL;
    }

    if (make_socket_non_blocking(tcp_socket) == -1) {
        perror("make_socket_non_blocking");
        close(tcp_socket);
        return NULL;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(tcp_socket);
        return NULL;
    }

    ev.events = EPOLLIN;
    ev.data.fd = tcp_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_socket, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        close(tcp_socket);
        close(epoll_fd);
        return NULL;
    }

    printf("[Thread %d] listening on port 8080...\n", thread_id);
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == tcp_socket) {
                while (1) {
                    socklen_t client_addrlen = sizeof(client_address);
                    int client_fd = accept(tcp_socket, (struct sockaddr *)&client_address, &client_addrlen);
                    if (client_fd == -1) {
                        break;
                    }

                    make_socket_non_blocking(client_fd);

                    client_context *ctx = calloc(1, sizeof(client_context));
                    if (!ctx) {
                        close(client_fd);
                        continue;
                    }
                    ctx->fd = client_fd;
                    ctx->current_state = METHOD;
                    ctx->curr = ctx->buffer;

                    struct epoll_event child_ev;
                    child_ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    child_ev.data.ptr = ctx;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &child_ev) == -1) {
                        close(client_fd);
                        free(ctx);
                    }
                }
            }
            else {
                client_context *ctx = (client_context *)events[i].data.ptr;
                bool close_conn = false;

                if (events[i].events & EPOLLIN) {
                    char tmp_buf[8192];
                    while (1) {
                        ssize_t bytes_read = read(ctx->fd, tmp_buf, sizeof(tmp_buf));

                        if (bytes_read > 0) {
                            if (ctx->total_bytes_read + (size_t)bytes_read >= BUFFERSIZE) {
                                size_t resp_len = 0;
                                char* err_resp = build_request_entity_too_large(&resp_len);
                                if (err_resp) {
                                    send_all(ctx->fd, err_resp, resp_len);
                                    free(err_resp);
                                }
                                close_conn = true;
                                break;
                            }

                            memcpy(&ctx->buffer[ctx->total_bytes_read], tmp_buf, (size_t)bytes_read);
                            ctx->total_bytes_read += (size_t)bytes_read;
                            ctx->buffer[ctx->total_bytes_read] = '\0';
                            ctx->bytes_to_read = ctx->total_bytes_read - ctx->bytes_parsed;
                            ctx->start = ctx->curr;

                            int parse_res = parse_request(ctx);
                            if (parse_res < 0) {
                                size_t resp_len = 0;
                                char* err_resp = NULL;
                                if (parse_res == -1) {
                                    err_resp = build_request_entity_too_large(&resp_len);
                                } 
                                else {
                                    err_resp = build_bad_request(&resp_len);
                                }
                                if (err_resp != NULL) {
                                    send_all(ctx->fd, err_resp, resp_len);
                                    free(err_resp);
                                }
                                close_conn = true;
                                break;
                            }
                            if (ctx->current_state == DONE) {
                                size_t resp_len = 0;
                                char* response = build_response(ctx, &resp_len);
                                if (response) {
                                    send_all(ctx->fd, response, resp_len);
                                    free(response);
                                }
                                close_conn = true;
                                break;
                            }
                        }
                        else if (bytes_read == 0) {
                            close_conn = true;
                            break;
                        }
                        else {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                close_conn = true;
                            }
                            break;
                        }
                    }
                }

                if (close_conn || (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ctx->fd, NULL);
                    close(ctx->fd);
                    free(ctx);
                }
            }
        }
    }

    close(tcp_socket);
    close(epoll_fd);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread_cycle, (void*)(intptr_t)i) != 0) {
            printf("Error creating thread: %d\n", errno);
            return -1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
