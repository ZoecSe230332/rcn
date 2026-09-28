#ifndef RCN_RCN_TYPES_H
#define RCN_RCN_TYPES_H

#include "rcn_platform.h"
#include "rcn_input_types.h"
#include "rcn_util.h"

U_DEFINE_ARR(epoll_stream_arr, struct epoll_stream*);
U_DEFINE_ARR(device_arr, struct device);
U_DEFINE_ARR(char_arr, char);

U_DEFINE_QUEUE(stream_queue, struct stream_item);

enum rcn_state {
    RCN_RUNNING,
    RCN_PAUSED,
};

enum fd_type {
    FD_USOCK,
    FD_RELAY,
    FD_ISOCK,
    FD_PEER,
    FD_UDEV,
    FD_DEV,
};

enum stream_type {
    STREAM_TYPE_SOCKET,
    STREAM_TYPE_DEVICE,
};

enum stream_state {
    STREAM_STREAMING_HEADER,
    STREAM_STREAMING_BODY,
    STREAM_STREAMING_COMPLETE,
    STREAM_STREAMING_CLOSED,
};

enum stream_operation {
    STREAM_OP_WRITING,
    STREAM_OP_READING,
};

struct stream_header {
    size_t size;
    int value;
};

struct stream_msg {
    struct stream_header header;
    void* buffer;
};

struct stream_item {
    union {
        struct stream_msg msg;
        struct input_event evt;
    } payload;
    size_t buffer_streamed;
    enum stream_type type;
    enum stream_state state;
    enum stream_operation op;
};

struct epoll_stream {
    struct stream_item fallback;
    struct stream_item* next;
    stream_queue queue;
    enum stream_operation default_op;
    enum stream_type default_type;
    enum fd_type fd_type;
    rcn_socket_t fd;
};

struct epoll_context {
    epoll_stream_arr stream_ptrs;    // stores pointers to struct epoll_stream
    rcn_socket_t epoll_fd;
};

enum daemon_type {
    DAEMON_SERVER,
    DAEMON_CLIENT,
};

#endif //RCN_RCN_TYPES_H
