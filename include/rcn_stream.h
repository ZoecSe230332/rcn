#ifndef RCN_RCN_STREAM_H
#define RCN_RCN_STREAM_H

#include "rcn_types.h"

int stream_stream(struct epoll_stream* stream);
int stream_init(struct epoll_stream* stream, rcn_socket_t fd, enum fd_type type);
int stream_queue_reading_socket(struct epoll_stream* stream);
int stream_queue_reading_device(struct epoll_stream* stream);
int stream_queue_writing_socket(struct epoll_context* ep_ctx, struct epoll_stream* stream, int header, size_t size, void* data);
int stream_queue_writing_device(struct epoll_context* ep_ctx, struct epoll_stream* stream, struct input_event event);
int stream_set_default(struct epoll_stream* stream, enum stream_operation default_op, enum stream_type default_type);
int stream_clear_fallback(struct epoll_stream* stream);
int stream_collect(struct epoll_stream* stream, struct stream_item** stream_data);
int stream_close(struct epoll_stream* stream);
int stream_shutdown(struct epoll_stream* stream);

#endif //RCN_RCN_STREAM_H
