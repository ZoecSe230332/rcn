#ifndef RCN_RCN_PEER_H
#define RCN_RCN_PEER_H

#include "rcn_platform.h"
#include "rcn_device.h"

struct d_context;

enum peer_msg_header {
    PEER_HEADER_DEV_CRT,
    PEER_HEADER_DEV_DEL,
    PEER_HEADER_EVENT,
    PEER_HEADER_PAUSE,
    PEER_HEADER_RESUME,
    PEER_HEADER_STOP,
};

struct peer_msg_dev_crt {
    struct device dev;
};

struct peer_msg_dev_del {
    size_t random_id;
};

enum peer_state {
    PEER_DISCONNECTED,
    PEER_CONNECTED,
};

struct peer_context {
    struct epoll_stream* isock_stream;
    struct epoll_stream* peer_stream;
    enum peer_state isock_state;
    enum peer_state peer_state;
};

int p_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item);
int p_init_peer_ctx(struct epoll_context* ep_ctx, struct peer_context* p_ctx, rcn_socket_t isock_fd, enum daemon_type d_type);
int p_close_peer_ctx(struct epoll_context* ep_ctx, struct peer_context* p_ctx);
int p_close_peer(struct epoll_context* ep_ctx, struct peer_context* p_ctx);

#endif //RCN_RCN_PEER_H
