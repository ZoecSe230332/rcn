#ifndef RCN_RCN_RELAY_H
#define RCN_RCN_RELAY_H

#include "rcn_platform.h"
#include "rcn_types.h"

#define DEFAULT_USOCK_COUNT 3
#define RELAY_SLEEP_TIMEOUT 5

struct d_context;

enum relay_msg_header {
    RELAY_HEADER_IDLE,
    RELAY_HEADER_START,
    RELAY_HEADER_PAUSE,
    RELAY_HEADER_PAUSE_AGAIN,
    RELAY_HEADER_RESUME,
    RELAY_HEADER_RESUME_AGAIN,
    RELAY_HEADER_STOP,
};

struct relay_msg {
    enum relay_msg_header header;
};

struct relay_arg {
    enum relay_msg_header header_sent;
    enum daemon_type d_type;
    bool sleep;
};

struct relay_context {
    epoll_stream_arr relay_streams;
    rcn_socket_t usock_fd;
};

typedef int (*r_handler_t)(struct relay_arg arg);

/* rcn_relay.c */
rcn_socket_t r_init_usock(const char* sock_path, size_t path_len);
int relay_start(struct relay_arg arg);
int r_close_relay(struct relay_context* r_ctx, struct epoll_stream* stream);
int r_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item);
int r_broadcast_relay_header(struct epoll_context* ep_ctx, epoll_stream_arr* relay_streams, enum relay_msg_header header);
int r_init_relay_ctx(struct epoll_context* ep_ctx, struct relay_context* r_ctx, rcn_socket_t usock_fd);
int r_close_relay_ctx(struct epoll_context* ep_ctx, struct relay_context* r_ctx);

#endif //RCN_RCN_RELAY_H
