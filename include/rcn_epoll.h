#ifndef RCN_RCN_EPOLL_H
#define RCN_RCN_EPOLL_H

#include "rcn_types.h"

#ifdef _WIN32
enum EPOLL_EVENTS {
    EPOLLIN = 0x001,
    EPOLLOUT = 0x004,
    EPOLLERR = 0x008,
    EPOLLHUP = 0x010,
    EPOLL_CTL_ADD = 1,
    EPOLL_CTL_DEL = 2,
    EPOLL_CTL_MOD = 3,
};

typedef union epoll_data {
    void *ptr;
    rcn_socket_t fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct epoll_event {
    uint32_t events;
    epoll_data_t data;
};
#else
#include <sys/epoll.h>
#endif

/* forward declarations */
struct d_context;
struct device;

int e_init_epoll_ctx(struct epoll_context* ep_ctx);
int e_close_epoll_ctx(struct epoll_context* ep_ctx);
int e_epoll_add(struct epoll_context* ep_ctx, rcn_socket_t fd, enum fd_type type);
int e_epoll_add_getr(struct epoll_context* ep_ctx, rcn_socket_t fd, enum fd_type type, struct epoll_stream** out_stream);
int e_epoll_add_device(struct epoll_context* ep_ctx, rcn_socket_t fd, struct device* device, enum fd_type type);
int e_epoll_close_remove(struct d_context* d_ctx, struct epoll_stream* stream);
int e_epoll_close_remove_simple(struct epoll_context* ep_ctx, struct epoll_stream* stream);
int e_epoll_sync_stream(struct epoll_context* ep_ctx, struct epoll_stream* stream);
int e_epoll_reset_stream(struct epoll_context* ep_ctx, struct epoll_stream* stream);
int e_epoll_wait(struct epoll_context* ep_ctx, struct epoll_event* events, int maxevents);

#endif //RCN_RCN_EPOLL_H
