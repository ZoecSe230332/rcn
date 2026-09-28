#include "include/rcn.h"
#include "include/rcn_epoll.h"
#include "include/rcn_stream.h"
#include <stdlib.h>
#include <string.h>

struct stream_info {
    void* buffer_offset;
    size_t size_total;
    size_t size_remaining;
};

int stream_init(struct epoll_stream* stream, rcn_socket_t fd, enum fd_type type) {
    stream->fd = fd;
    stream->next = &stream->fallback;
    CHECK(u_queue_init(&stream->queue.r, sizeof(struct stream_item), RCN_STD_CAPACITY) == -1);
    switch (type) {
        case FD_PEER: CHECK(stream_set_default(stream, STREAM_OP_READING, STREAM_TYPE_SOCKET) == -1); break;
        case FD_RELAY: CHECK(stream_set_default(stream, STREAM_OP_READING, STREAM_TYPE_SOCKET) == -1); break;
        case FD_DEV: CHECK(stream_set_default(stream, STREAM_OP_READING, STREAM_TYPE_DEVICE) == -1); break;
        case FD_UDEV: CHECK(stream_set_default(stream, STREAM_OP_WRITING, STREAM_TYPE_DEVICE) == -1); break;
        case FD_USOCK: break;
        case FD_ISOCK: break;
    }
    return 0;
err:
    ERR_LOG("stream_setup");
    return -1;
}

static int stream_queue_reading(struct epoll_stream* stream, enum stream_type type) {
    struct stream_item stream_data = { 0 };
    stream_data.state = STREAM_STREAMING_HEADER;
    stream_data.op = STREAM_OP_READING;
    stream_data.type = type;
    if (type == STREAM_TYPE_SOCKET) {
        stream_data.payload.msg.header = (struct stream_header){ 0 };
    } else if (type == STREAM_TYPE_DEVICE) {
        stream_data.payload.evt = (struct input_event){ 0 };
    }
    stream_data.buffer_streamed = 0;
    CHECK(u_queue_push(&stream->queue.r, &stream_data) == -1);
    CHECK(u_queue_peek(&stream->queue.r, (void**)&stream->next) == -1);
    return 0;
err:
    ERR_LOG("stream_set_reading");
    return -1;
}

int stream_queue_reading_socket(struct epoll_stream* stream) {
    CHECK(stream_queue_reading(stream, STREAM_TYPE_SOCKET) == -1);
    return 0;
err:
    ERR_LOG("stream_queue_reading_socket");
    return -1;
}

int stream_queue_reading_device(struct epoll_stream* stream) {
    CHECK(stream_queue_reading(stream, STREAM_TYPE_DEVICE) == -1);
    return 0;
err:
    ERR_LOG("stream_queue_reading_device");
    return -1;
}

static int stream_queue_writing(struct epoll_context* ep_ctx, struct epoll_stream* stream, enum stream_type type, int header, size_t size, void* data) {
    struct stream_item stream_data = { 0 };
    stream_data.state = STREAM_STREAMING_HEADER;
    stream_data.op = STREAM_OP_WRITING;
    stream_data.type = type;
    if (type == STREAM_TYPE_SOCKET) {
        stream_data.payload.msg.header.value = header;
        stream_data.payload.msg.header.size = size;
        stream_data.buffer_streamed = 0;
        if (size > 0) {
            stream_data.payload.msg.buffer = calloc(1, size);
            CHECK(stream_data.payload.msg.buffer == NULL);
            memcpy(stream_data.payload.msg.buffer, data, size);
        } else {
            stream_data.payload.msg.buffer = NULL;
        }
    } else if (type == STREAM_TYPE_DEVICE) {
        stream_data.payload.evt = *(struct input_event*)data;
    }
    CHECK(u_queue_push(&stream->queue.r, &stream_data) == -1);
    CHECK(u_queue_peek(&stream->queue.r, (void**)&stream->next) == -1);
    CHECK(e_epoll_sync_stream(ep_ctx, stream) == -1);
    return 0;
err:
    ERR_LOG("stream_set_writing");
    return -1;
}

int stream_queue_writing_socket(struct epoll_context* ep_ctx, struct epoll_stream* stream, int header, size_t size, void* data) {
    CHECK(stream_queue_writing(ep_ctx, stream, STREAM_TYPE_SOCKET, header, size, data) == -1);
    return 0;
err:
    ERR_LOG("stream_queue_writing_socket");
    return -1;
}

int stream_queue_writing_device(struct epoll_context* ep_ctx, struct epoll_stream* stream, struct input_event event) {
    CHECK(stream_queue_writing(ep_ctx, stream, STREAM_TYPE_DEVICE, 0, 0, &event) == -1);
    return 0;
err:
    ERR_LOG("stream_queue_writing_device");
    return -1;
}

int stream_set_default(struct epoll_stream* stream, enum stream_operation default_op, enum stream_type default_type) {
    CHECK(stream == NULL);
    struct stream_item* stream_item = &stream->fallback;
    if (stream_item->type == STREAM_TYPE_SOCKET && stream_item->payload.msg.buffer != NULL)
        u_safe_free(&stream_item->payload.msg.buffer);
    stream_item->buffer_streamed = 0;
    stream_item->state = STREAM_STREAMING_HEADER;
    stream->default_op = default_op;
    stream_item->op = default_op;
    stream->default_type = default_type;
    stream_item->type = default_type;
    return 0;
err:
    ERR_LOG("stream_set");
    return -1;
}

int stream_clear_fallback(struct epoll_stream* stream) {
    CHECK(stream_set_default(stream, stream->default_op, stream->default_type) == -1);
    return 0;
err:
    ERR_LOG("stream_clear");
    return -1;
}

static int calc_stream_info(struct stream_item* stream_item, struct stream_info* stream_info) {
    enum stream_type type = stream_item->type;
    void* data_ptr = NULL;
    if (type == STREAM_TYPE_SOCKET) {
        stream_info->size_total = sizeof(struct stream_header);
        data_ptr = &stream_item->payload.msg.header;
        if (stream_item->state == STREAM_STREAMING_BODY) {
            if (stream_item->op == STREAM_OP_READING && stream_item->payload.msg.buffer == NULL) {
                stream_item->payload.msg.buffer = calloc(1, stream_item->payload.msg.header.size);
                CHECK(stream_item->payload.msg.buffer == NULL);
            }
            stream_info->size_total = stream_item->payload.msg.header.size;
            data_ptr = stream_item->payload.msg.buffer;
        }
    } else if (type == STREAM_TYPE_DEVICE) {
        stream_info->size_total = sizeof(struct input_event);
        data_ptr = &stream_item->payload.evt;
    } else {
        goto err;
    }
    // calculate buffer offset to send and remaining size independent of state
    stream_info->buffer_offset = (char*)data_ptr + stream_item->buffer_streamed;
    stream_info->size_remaining = stream_info->size_total - stream_item->buffer_streamed;
    return 0;
err:
    ERR_LOG("calc_stream_info");
    return -1;
}

int stream_stream(struct epoll_stream* stream) {
    struct stream_item* stream_item = stream->next;
    CHECK(stream_item->state == STREAM_STREAMING_COMPLETE);
    struct stream_info stream_info = { 0 };
    CHECK(calc_stream_info(stream_item, &stream_info) == -1);
    ssize_t streamed = 0;

    // stream actual data
    if (stream_item->op == STREAM_OP_READING) {
        if (stream_item->type == STREAM_TYPE_SOCKET) {
            streamed = rcn_recv(stream->fd, stream_info.buffer_offset, stream_info.size_remaining);
        } else {
#ifndef _WIN32
            streamed = read(stream->fd, stream_info.buffer_offset, stream_info.size_remaining);
#else
            streamed = -1;
#endif
        }
    } else if (stream_item->op == STREAM_OP_WRITING) {
        if (stream_item->type == STREAM_TYPE_SOCKET) {
            streamed = rcn_send(stream->fd, stream_info.buffer_offset, stream_info.size_remaining);
        } else {
#ifndef _WIN32
            streamed = write(stream->fd, stream_info.buffer_offset, stream_info.size_remaining);
#else
            streamed = -1;
#endif
        }
    } else {
        ERR_GOTO(err, "err: invalid stream op: %d\n", stream_item->op);
    }

    // check for errors
    if (streamed <= 0) {
        if (streamed < 0) {
            if (stream_item->type == STREAM_TYPE_SOCKET) {
                int err_code = rcn_get_socket_error();
                if (rcn_is_blocking_error(err_code)) {
                    return 0;
                }
            } else {
#ifndef _WIN32
                if (errno == ENODEV || errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (errno == ENODEV) stream_item->state = STREAM_STREAMING_CLOSED;
                    return 0;
                }
#endif
            }
        }
        stream_item->state = STREAM_STREAMING_CLOSED;
        return 0;
    }

    // update state after streaming
    stream_item->buffer_streamed += streamed;
    CHECK(stream_item->buffer_streamed > stream_info.size_total);
    if (stream_item->buffer_streamed == stream_info.size_total) {
        stream_item->buffer_streamed = 0;
        if (stream_item->type == STREAM_TYPE_DEVICE)
            stream_item->state = STREAM_STREAMING_COMPLETE;
        if (stream_item->state == STREAM_STREAMING_BODY || (stream_item->type == STREAM_TYPE_SOCKET && stream_item->payload.msg.header.size == 0))
            stream_item->state = STREAM_STREAMING_COMPLETE;
        else if (stream_item->state == STREAM_STREAMING_HEADER)
            stream_item->state = STREAM_STREAMING_BODY;
    }
    return 0;
err:
    stream_item->state = STREAM_STREAMING_CLOSED;
    ERR_LOG("stream_stream");
    return -1;
}

int stream_collect(struct epoll_stream* stream, struct stream_item** stream_data) {
    *stream_data = &stream->fallback;
    if (stream->queue.r.is_empty == false) {
        CHECK(u_queue_pop(&stream->queue.r, *stream_data) == -1);
    }
    if (stream->queue.r.is_empty == true)
        stream->next = &stream->fallback;
    else
        CHECK(u_queue_peek(&stream->queue.r, (void**)&stream->next) == -1);
    return 0;
err:
    ERR_LOG("stream_collect");
    return -1;
}

int stream_close(struct epoll_stream* stream) {
    while (stream->queue.r.is_empty == false) {
        struct stream_item stream_item = { 0 };
        CHECK(u_queue_pop(&stream->queue.r, &stream_item) == -1);
        if (stream_item.payload.msg.buffer != NULL)
            u_safe_free(&stream_item.payload.msg.buffer);
    }
    CHECK(u_queue_free(&stream->queue.r) == -1);
    if (stream->fallback.payload.msg.buffer != NULL)
        u_safe_free(&stream->fallback.payload.msg.buffer);
    if (stream->fd != RCN_INVALID_SOCKET) {
        rcn_close_socket(stream->fd);
        stream->fd = RCN_INVALID_SOCKET;
    }
    return 0;
err:
    ERR_LOG("stream_close");
    return -1;
}

int stream_shutdown(struct epoll_stream* stream) {
    CHECK(u_close_connection(stream->fd) == -1);
    CHECK(stream_close(stream) == -1);
    return 0;
err:
    ERR_LOG("stream_shutdown");
    return -1;
}