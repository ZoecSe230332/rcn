#include "include/rcn.h"
#include "include/rcn_daemon.h"
#include "include/rcn_epoll.h"
#include "include/rcn_relay.h"

#include <signal.h>

#include "include/rcn_stream.h"
#include "include/rcn_types.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/un.h>
#include <unistd.h>

struct sock_info {
    char* sock_path;
    size_t path_len;
};

static const char* relay_text[] = {
    [RELAY_HEADER_IDLE] = "",
    [RELAY_HEADER_START] = "started",
    [RELAY_HEADER_PAUSE] = "paused",
    [RELAY_HEADER_PAUSE_AGAIN] = "already paused",
    [RELAY_HEADER_RESUME] = "resumed",
    [RELAY_HEADER_RESUME_AGAIN] = "already resumed",
    [RELAY_HEADER_STOP] = "stopped",
};

static int init_sockinfo(enum daemon_type d_type, struct sock_info* info) {
    if (d_type == DAEMON_SERVER) {
        info->sock_path = RCN_SERVER_SOCKET_PATH;
        info->path_len = RCN_SERVER_SOCKET_LEN;
    } else if (d_type == DAEMON_CLIENT) {
        info->sock_path = RCN_CLIENT_SOCKET_PATH;
        info->path_len = RCN_CLIENT_SOCKET_LEN;
    } else {
        goto err;
    }
    return 0;
err:
    ERR_LOG("init_sockinfo");
    return -1;
}

int r_init_usock(char* sock_path, size_t path_len) {
    unlink(sock_path);
    int d_usock_fd = TRY(socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0), -1);
    struct sockaddr_un d_uaddr = {
        .sun_family = AF_UNIX,
    };
    memcpy(d_uaddr.sun_path, sock_path, path_len);
    CHECK(bind(d_usock_fd, (struct sockaddr*)&d_uaddr, sizeof(d_uaddr)) == -1);
    CHECK(listen(d_usock_fd, DEFAULT_USOCK_COUNT) == -1);
    return d_usock_fd;
err:
    ERR_LOG("r_init_usock");
    return -1;
}

static int connect_usock(char* sock_path, size_t path_len) {
    int usock_fd = TRY(socket(AF_UNIX, SOCK_STREAM, 0), -1);
    struct sockaddr_un r_uaddr = {};
    r_uaddr.sun_family = AF_UNIX;
    memcpy(r_uaddr.sun_path, sock_path, path_len);
    CHECK(connect(usock_fd, (struct sockaddr*)&r_uaddr, sizeof(r_uaddr)) == -1);
    return usock_fd;
err:
    ERR_LOG("r_init_usock");
    return -1;
}

int r_broadcast_relay_header(struct epoll_context* ep_ctx, epoll_stream_arr* relay_streams, enum relay_msg_header header) {
    for (size_t i = 0; i < relay_streams->r.length; i++) {
        struct epoll_stream* relay_stream = {};
        CHECK(u_array_getv(&relay_streams->r, &relay_stream, i) == -1);
        CHECK(stream_queue_writing_socket(ep_ctx, relay_stream, header, 0, NULL) == -1);
    }
    return 0;
err:
    ERR_LOG("d_broadcast_relay_header");
    return -1;
}

void tstp_handler(int sig) {
    (void)sig;
    fprintf(stderr, "\rerr: cannot start daemon, see log for more info\n");
    exit(-1);
}

void cont_handler(int sig) {
    (void)sig;
}

static int sleep_usock() {
    printf("...");
    fflush(stdout);
    signal(SIGCONT, cont_handler);
    signal(SIGTSTP, tstp_handler);
    const unsigned int remaining = sleep(RELAY_SLEEP_TIMEOUT);
    CHECK(remaining == 0);
    return 0;
err:
    errno = ETIMEDOUT;
    fprintf(stderr, "\rerr: sleep_usock: relay timeout, no response from daemon\n");
    return -1;
}

int relay_start(struct relay_arg arg) {
    if (arg.sleep == true)
        CHECK(sleep_usock() == -1);
    CHECK(prctl(PR_SET_NAME, RCN_PROC_NAME_RELAY, 0UL, 0UL, 0UL) == -1);
    struct sock_info info = {};
    CHECK(init_sockinfo(arg.d_type, &info) == -1);
    int usock_fd = TRY(connect_usock(info.sock_path, info.path_len), -1);
    printf("\rrcn>");
    fflush(stdout);
    struct stream_header msg = {};
    msg = (struct stream_header){ .value = arg.header_sent, .size = 0 };
    CHECK(write(usock_fd, &msg, sizeof(msg)) == -1);
    // blocking read on socket to wait for daemon
    CHECK(read(usock_fd, &msg, sizeof(msg)) == -1);
    CHECK(u_close_connection(usock_fd) == -1);
    printf("\rrcn: %s\n", relay_text[msg.value]);
    return 0;
err:
    fprintf(stderr, "\r");
    ERR_LOG("r_await");
    return -1;
}

int r_close_relay(struct relay_context* r_ctx, struct epoll_stream* stream) {
    size_t index = TRY(u_array_find_index(&r_ctx->relay_streams.r, &stream), -1);
    CHECK(u_array_remove(&r_ctx->relay_streams.r, index) == -1);
    return 0;
err:
    ERR_LOG("r_close_relay");
    return -1;
}

static int handler_start(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    (void)stream_item;
    CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, stream, RELAY_HEADER_START, 0, NULL) == -1);
    return 0;
err:
    ERR_LOG("handler_start");
    return -1;
}

static int handler_pause(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    (void)stream_item;
    if (d_ctx->state == RCN_PAUSED) {
        CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, stream, RELAY_HEADER_PAUSE_AGAIN, 0, NULL) == -1);
        return 0;
    }
    if (d_ctx->type == DAEMON_CLIENT)
        CHECK(dev_ctrl_devices(d_ctx, &d_ctx->device_ctx->devices, DEV_CTRL_RELEASE) == -1);
    else if (d_ctx->type == DAEMON_SERVER)
        CHECK(dev_release_virt_keys_all(d_ctx->ep_ctx, &d_ctx->device_ctx->devices) == -1);
    epoll_stream_arr* relay_streams = &d_ctx->relay_ctx->relay_streams;
    CHECK(r_broadcast_relay_header(d_ctx->ep_ctx, relay_streams, RELAY_HEADER_PAUSE) == -1);
    CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, d_ctx->peer_ctx->peer_stream, PEER_HEADER_PAUSE, 0, NULL) == -1);
    d_ctx->state = RCN_PAUSED;
    return 0;
err:
    ERR_LOG("handler_pause");
    return -1;
}

static int handler_resume(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    (void)stream_item;
    if (d_ctx->state == RCN_RUNNING) {
        CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, stream, RELAY_HEADER_RESUME_AGAIN, 0, NULL) == -1);
        return 0;
    }
    if (d_ctx->type == DAEMON_CLIENT)
        CHECK(dev_ctrl_devices(d_ctx, &d_ctx->device_ctx->devices, DEV_CTRL_CAPTURE) == -1);
    else if (d_ctx->type == DAEMON_SERVER)
        CHECK(dev_release_virt_keys_all(d_ctx->ep_ctx, &d_ctx->device_ctx->devices) == -1);
    CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, d_ctx->peer_ctx->peer_stream, PEER_HEADER_RESUME, 0, NULL) == -1);
    d_ctx->state = RCN_RUNNING;
    return 0;
err:
    ERR_LOG("handler_resume");
    return -1;
}

static int handler_stop(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    (void)d_ctx;
    (void)stream;
    (void)stream_item;
    d_ctx->exit = true;
    epoll_stream_arr* relay_streams = &d_ctx->relay_ctx->relay_streams;
    CHECK(r_broadcast_relay_header(d_ctx->ep_ctx, relay_streams, RELAY_HEADER_STOP) == -1);
    CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, d_ctx->peer_ctx->peer_stream, PEER_HEADER_STOP, 0, NULL) == -1);
    return 0;
err:
    ERR_LOG("handler_stop");
    return -1;
}

int r_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    switch (stream_item->payload.msg.header.value) {
        case RELAY_HEADER_IDLE: {
            /* do nothing, relay hangs on blocking read() */
            break;
        }
        case RELAY_HEADER_START: {
            CHECK(handler_start(d_ctx, stream, stream_item) == -1);
            break;
        }
        case RELAY_HEADER_PAUSE: {
            CHECK(handler_pause(d_ctx, stream, stream_item) == -1);
            break;
        }
        case RELAY_HEADER_RESUME: {
            CHECK(handler_resume(d_ctx, stream, stream_item) == -1);
            break;
        }
        case RELAY_HEADER_STOP: {
            CHECK(handler_stop(d_ctx, stream, stream_item) == -1);
            break;
        }
        default: ERR_GOTO(err, "err: unknown relay header '%d'\n", stream_item->payload.msg.header.value);
    }
    return 0;
err:
    ERR_LOG("r_handler");
    return -1;
}

int r_init_relay_ctx(struct epoll_context* ep_ctx, struct relay_context* r_ctx, int usock_fd) {
    r_ctx->usock_fd = usock_fd;
    CHECK(u_array_init(&r_ctx->relay_streams.r, sizeof(struct relay*), RCN_STD_CAPACITY) == -1);
    CHECK(e_epoll_add(ep_ctx, usock_fd, FD_USOCK) == -1);
    return 0;
err:
    ERR_LOG("d_init_relay_ctx");
    return -1;
}

int r_close_relay_ctx(struct epoll_context* ep_ctx, struct relay_context* r_ctx) {
    // do not close usock_fd, its closed in close_epoll_ctx
    for (size_t i = 0; i < r_ctx->relay_streams.r.length; i++) {
        struct epoll_stream* r_stream = {};
        CHECK(u_array_getr(&r_ctx->relay_streams.r, (void**)&r_stream, i) == -1);
        CHECK(e_epoll_close_remove_simple(ep_ctx, r_stream) == -1);
    }
    CHECK(u_array_free(&r_ctx->relay_streams.r) == -1);
    return 0;
err:
    ERR_LOG("r_close_relay_ctx");
    return -1;
}