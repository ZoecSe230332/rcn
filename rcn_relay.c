#include "include/rcn.h"
#include "include/rcn_daemon.h"
#include "include/rcn_epoll.h"
#include "include/rcn_relay.h"
#include "include/rcn_stream.h"
#include "include/rcn_types.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/prctl.h>
#include <sys/un.h>
#include <unistd.h>
#endif

struct sock_info {
    const char* sock_path;
    size_t path_len;
};

static const char* relay_text[] = {
    "",                 // RELAY_HEADER_IDLE
    "started",          // RELAY_HEADER_START
    "paused",           // RELAY_HEADER_PAUSE
    "already paused",   // RELAY_HEADER_PAUSE_AGAIN
    "resumed",          // RELAY_HEADER_RESUME
    "already resumed",  // RELAY_HEADER_RESUME_AGAIN
    "stopped",          // RELAY_HEADER_STOP
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

rcn_socket_t r_init_usock(const char* sock_path, size_t path_len) {
    (void)path_len;
#ifndef _WIN32
    unlink(sock_path);
#else
    DeleteFileA(sock_path);
#endif

    rcn_socket_t d_usock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(d_usock_fd == RCN_INVALID_SOCKET);

    CHECK(rcn_set_nonblocking(d_usock_fd) == -1);

    struct sockaddr_un d_uaddr;
    memset(&d_uaddr, 0, sizeof(d_uaddr));
    d_uaddr.sun_family = AF_UNIX;
    strncpy(d_uaddr.sun_path, sock_path, sizeof(d_uaddr.sun_path) - 1);

    CHECK(bind(d_usock_fd, (struct sockaddr*)&d_uaddr, sizeof(d_uaddr)) == -1);
    CHECK(listen(d_usock_fd, DEFAULT_USOCK_COUNT) == -1);
    return d_usock_fd;
err:
    if (d_usock_fd != RCN_INVALID_SOCKET)
        rcn_close_socket(d_usock_fd);
    ERR_LOG("r_init_usock");
    return RCN_INVALID_SOCKET;
}

static rcn_socket_t connect_usock(const char* sock_path, size_t path_len) {
    (void)path_len;
    rcn_socket_t usock_fd = RCN_INVALID_SOCKET;

    // Retry connection up to 50 times (5 seconds total) to allow daemon startup
    for (int retry = 0; retry < 50; retry++) {
        usock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (usock_fd == RCN_INVALID_SOCKET) {
            rcn_sleep_ms(100);
            continue;
        }

        struct sockaddr_un r_uaddr;
        memset(&r_uaddr, 0, sizeof(r_uaddr));
        r_uaddr.sun_family = AF_UNIX;
        strncpy(r_uaddr.sun_path, sock_path, sizeof(r_uaddr.sun_path) - 1);

        if (connect(usock_fd, (struct sockaddr*)&r_uaddr, sizeof(r_uaddr)) == 0) {
            return usock_fd;
        }

        int err = rcn_get_socket_error();
        rcn_close_socket(usock_fd);
        usock_fd = RCN_INVALID_SOCKET;
        if (retry == 0) {
            fprintf(stderr, "\nconnect_usock attempt failed, err=%d\n", err);
        }
        rcn_sleep_ms(100);
    }
    int err = rcn_get_socket_error();
    fprintf(stderr, "err: connect_usock final err: %d\n", err);
    return RCN_INVALID_SOCKET;
}

int r_broadcast_relay_header(struct epoll_context* ep_ctx, epoll_stream_arr* relay_streams, enum relay_msg_header header) {
    for (size_t i = 0; i < relay_streams->r.length; i++) {
        struct epoll_stream* relay_stream = NULL;
        CHECK(u_array_getv(&relay_streams->r, (void*)&relay_stream, i) == -1);
        CHECK(stream_queue_writing_socket(ep_ctx, relay_stream, header, 0, NULL) == -1);
    }
    return 0;
err:
    ERR_LOG("d_broadcast_relay_header");
    return -1;
}

#ifndef _WIN32
static void tstp_handler(int sig) {
    (void)sig;
    fprintf(stderr, "\rerr: cannot start daemon, see log for more info\n");
    exit(-1);
}

static void cont_handler(int sig) {
    (void)sig;
}

static int sleep_usock(void) {
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
#else
static int sleep_usock_win(enum daemon_type d_type) {
    printf("...");
    fflush(stdout);
    HANDLE hReadyEvent = OpenEventA(SYNCHRONIZE, FALSE, (d_type == DAEMON_SERVER) ? "rcn_server_ready" : "rcn_client_ready");
    if (hReadyEvent) {
        DWORD wait_res = WaitForSingleObject(hReadyEvent, 5000);
        CloseHandle(hReadyEvent);
        if (wait_res != WAIT_OBJECT_0) {
            fprintf(stderr, "\rerr: timeout waiting for daemon to initialize\n");
            return -1;
        }
    } else {
        rcn_sleep_ms(200);
    }
    return 0;
}
#endif

int relay_start(struct relay_arg arg) {
    if (arg.sleep == true) {
#ifndef _WIN32
        CHECK(sleep_usock() == -1);
#else
        CHECK(sleep_usock_win(arg.d_type) == -1);
#endif
    }
#ifndef _WIN32
    prctl(PR_SET_NAME, RCN_PROC_NAME_RELAY, 0UL, 0UL, 0UL);
#endif
    struct sock_info info = { 0 };
    CHECK(init_sockinfo(arg.d_type, &info) == -1);
    rcn_socket_t usock_fd = connect_usock(info.sock_path, info.path_len);
    CHECK(usock_fd == RCN_INVALID_SOCKET);

    printf("\rrcn>");
    fflush(stdout);
    struct stream_header msg = { 0 };
    msg.value = arg.header_sent;
    msg.size = 0;

    int sent = rcn_send(usock_fd, &msg, sizeof(msg));
    CHECK(sent != (int)sizeof(msg));

    // blocking read on socket to wait for daemon response
    int recvd = rcn_recv(usock_fd, &msg, sizeof(msg));
    CHECK(recvd != (int)sizeof(msg));

    CHECK(u_close_connection(usock_fd) == -1);
    if (msg.value >= 0 && (size_t)msg.value < sizeof(relay_text) / sizeof(relay_text[0])) {
        printf("\rrcn: %s\n", relay_text[msg.value]);
    }
    return 0;
err:
    fprintf(stderr, "\r");
    ERR_LOG("r_await");
    return -1;
}

int r_close_relay(struct relay_context* r_ctx, struct epoll_stream* stream) {
    ssize_t index = u_array_find_index(&r_ctx->relay_streams.r, &stream);
    CHECK(index == -1);
    CHECK(u_array_remove(&r_ctx->relay_streams.r, (size_t)index) == -1);
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
    if (d_ctx->peer_ctx && d_ctx->peer_ctx->peer_stream)
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
    epoll_stream_arr* relay_streams = &d_ctx->relay_ctx->relay_streams;
    CHECK(r_broadcast_relay_header(d_ctx->ep_ctx, relay_streams, RELAY_HEADER_RESUME) == -1);
    if (d_ctx->peer_ctx && d_ctx->peer_ctx->peer_stream)
        CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, d_ctx->peer_ctx->peer_stream, PEER_HEADER_RESUME, 0, NULL) == -1);
    d_ctx->state = RCN_RUNNING;
    return 0;
err:
    ERR_LOG("handler_resume");
    return -1;
}

static int handler_stop(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    (void)stream;
    (void)stream_item;
    d_ctx->exit = true;

    // 1. Release virtual keys immediately so no keys stay held down
    if (d_ctx->type == DAEMON_SERVER) {
        dev_release_virt_keys_all(d_ctx->ep_ctx, &d_ctx->device_ctx->devices);
    } else if (d_ctx->type == DAEMON_CLIENT) {
        dev_ctrl_devices(d_ctx, &d_ctx->device_ctx->devices, DEV_CTRL_RELEASE);
    }

    // 2. If a client/server peer is connected, notify and disconnect immediately
    if (d_ctx->peer_ctx) {
        if (d_ctx->peer_ctx->peer_stream && d_ctx->peer_ctx->peer_stream->fd != RCN_INVALID_SOCKET) {
            struct stream_header stop_hdr = { .size = 0, .value = PEER_HEADER_STOP };
            rcn_send(d_ctx->peer_ctx->peer_stream->fd, &stop_hdr, sizeof(stop_hdr));
            rcn_shutdown_socket(d_ctx->peer_ctx->peer_stream->fd);
            rcn_close_socket(d_ctx->peer_ctx->peer_stream->fd);
            d_ctx->peer_ctx->peer_stream->fd = RCN_INVALID_SOCKET;
            d_ctx->peer_ctx->peer_state = PEER_DISCONNECTED;
        }
        if (d_ctx->peer_ctx->isock_stream && d_ctx->peer_ctx->isock_stream->fd != RCN_INVALID_SOCKET) {
            rcn_close_socket(d_ctx->peer_ctx->isock_stream->fd);
            d_ctx->peer_ctx->isock_stream->fd = RCN_INVALID_SOCKET;
            d_ctx->peer_ctx->isock_state = PEER_DISCONNECTED;
        }
    }

    // 3. Notify relay callers that we have stopped
    epoll_stream_arr* relay_streams = &d_ctx->relay_ctx->relay_streams;
    CHECK(r_broadcast_relay_header(d_ctx->ep_ctx, relay_streams, RELAY_HEADER_STOP) == -1);
    return 0;
err:
    ERR_LOG("handler_stop");
    return -1;
}

int r_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    switch (stream_item->payload.msg.header.value) {
        case RELAY_HEADER_IDLE: {
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

int r_init_relay_ctx(struct epoll_context* ep_ctx, struct relay_context* r_ctx, rcn_socket_t usock_fd) {
    r_ctx->usock_fd = usock_fd;
    CHECK(u_array_init(&r_ctx->relay_streams.r, sizeof(struct epoll_stream*), RCN_STD_CAPACITY) == -1);
    CHECK(e_epoll_add(ep_ctx, usock_fd, FD_USOCK) == -1);
    return 0;
err:
    ERR_LOG("d_init_relay_ctx");
    return -1;
}

int r_close_relay_ctx(struct epoll_context* ep_ctx, struct relay_context* r_ctx) {
    for (size_t i = 0; i < r_ctx->relay_streams.r.length; i++) {
        struct epoll_stream* r_stream = NULL;
        CHECK(u_array_getr(&r_ctx->relay_streams.r, (void**)&r_stream, i) == -1);
        CHECK(e_epoll_close_remove_simple(ep_ctx, r_stream) == -1);
    }
    CHECK(u_array_free(&r_ctx->relay_streams.r) == -1);
    return 0;
err:
    ERR_LOG("r_close_relay_ctx");
    return -1;
}