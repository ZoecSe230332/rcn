#include "include/rcn.h"
#include "include/rcn_epoll.h"
#include "include/rcn_stream.h"
#include "include/rcn_daemon.h"
#include "include/rcn_device.h"
#include <stdlib.h>
#include <string.h>

int e_init_epoll_ctx(struct epoll_context* ep_ctx) {
#ifndef _WIN32
    ep_ctx->epoll_fd = epoll_create1(0);
    CHECK(ep_ctx->epoll_fd == -1);
#else
    ep_ctx->epoll_fd = 0;
    RegisterHotKey(NULL, 0x5243, MOD_CONTROL | MOD_ALT, VK_ESCAPE);
    RegisterHotKey(NULL, 0x5244, MOD_CONTROL | MOD_ALT, VK_PAUSE);
#endif
    CHECK(u_array_init(&ep_ctx->stream_ptrs.r, sizeof(struct epoll_stream*), RCN_STD_CAPACITY) == -1);
    return 0;
err:
    ERR_LOG("d_init_epoll");
    return -1;
}

int e_close_epoll_ctx(struct epoll_context* ep_ctx) {
    while (ep_ctx->stream_ptrs.r.length > 0) {
        struct epoll_stream* e_stream = NULL;
        CHECK(u_array_getv(&ep_ctx->stream_ptrs.r, (void*)&e_stream, 0) == -1);
        CHECK(e_epoll_close_remove_simple(ep_ctx, e_stream) == -1);
    }
    CHECK(u_array_free(&ep_ctx->stream_ptrs.r) == -1);
#ifndef _WIN32
    if (ep_ctx->epoll_fd != -1) {
        close(ep_ctx->epoll_fd);
        ep_ctx->epoll_fd = -1;
    }
#else
    UnregisterHotKey(NULL, 0x5243);
    UnregisterHotKey(NULL, 0x5244);
#endif
    return 0;
err:
    ERR_LOG("e_close_epoll_ctx");
    return -1;
}

int e_epoll_add_getr(struct epoll_context* ep_ctx, rcn_socket_t fd, enum fd_type type, struct epoll_stream** out_stream) {
    struct epoll_stream* stream = (struct epoll_stream*)calloc(1, sizeof(struct epoll_stream));
    CHECK(stream == NULL);
    stream->fd_type = type;
    CHECK(stream_init(stream, fd, type) == -1);
    CHECK(u_array_add(&ep_ctx->stream_ptrs.r, &stream) == -1);

#ifndef _WIN32
    struct epoll_event u_evt = { 0 };
    u_evt.events = EPOLLIN;
    u_evt.data.ptr = stream;
    CHECK(epoll_ctl(ep_ctx->epoll_fd, EPOLL_CTL_ADD, fd, &u_evt) == -1);
#endif

    *out_stream = stream;
    return 0;
err:
    if (stream != NULL)
        free(stream);
    ERR_LOG("d_epoll_add");
    return -1;
}

int e_epoll_add(struct epoll_context* ep_ctx, rcn_socket_t fd, enum fd_type type) {
    struct epoll_stream* tmp_stream = NULL;
    CHECK(e_epoll_add_getr(ep_ctx, fd, type, &tmp_stream) == -1);
    return 0;
err:
    ERR_LOG("d_epoll_add");
    return -1;
}

int e_epoll_add_device(struct epoll_context* ep_ctx, rcn_socket_t fd, struct device* device, enum fd_type type) {
    struct epoll_stream* stream = (struct epoll_stream*)calloc(1, sizeof(struct epoll_stream));
    CHECK(stream == NULL);
    CHECK(stream_init(stream, fd, type) == -1);
    CHECK(u_array_add(&ep_ctx->stream_ptrs.r, &stream) == -1);
    stream->fd_type = type;
    device->stream = stream;

#ifndef _WIN32
    struct epoll_event u_evt = { 0 };
    u_evt.events = EPOLLIN;
    u_evt.data.ptr = stream;
    CHECK(epoll_ctl(ep_ctx->epoll_fd, EPOLL_CTL_ADD, fd, &u_evt) == -1);
#endif

    return 0;
err:
    if (stream != NULL)
        free(stream);
    ERR_LOG("d_epoll_add");
    return -1;
}

int e_epoll_reset_stream(struct epoll_context* ep_ctx, struct epoll_stream* stream) {
    CHECK(stream_clear_fallback(stream) == -1);
    CHECK(e_epoll_sync_stream(ep_ctx, stream) == -1);
    return 0;
err:
    ERR_LOG("epoll_reset_stream");
    return -1;
}

int e_epoll_sync_stream(struct epoll_context* ep_ctx, struct epoll_stream* stream) {
#ifndef _WIN32
    uint32_t events = stream->next->op == STREAM_OP_WRITING ? EPOLLOUT : EPOLLIN;
    struct epoll_event evt = { 0 };
    evt.events = events;
    evt.data.ptr = stream;
    CHECK(epoll_ctl(ep_ctx->epoll_fd, EPOLL_CTL_MOD, stream->fd, &evt) == -1);
#else
    (void)ep_ctx;
    (void)stream;
#endif
    return 0;
#ifndef _WIN32
err:
    ERR_LOG("d_epoll_entry_update");
    return -1;
#endif
}

int e_epoll_close_remove(struct d_context* d_ctx, struct epoll_stream* stream) {
    switch (stream->fd_type) {
        case FD_RELAY: r_close_relay(d_ctx->relay_ctx, stream); break;
        case FD_DEV: dev_close_dev(d_ctx->device_ctx, stream); break;
        case FD_ISOCK: p_close_peer(d_ctx->ep_ctx, d_ctx->peer_ctx); break;
        case FD_PEER: {
            p_close_peer(d_ctx->ep_ctx, d_ctx->peer_ctx);
            if (d_ctx->type == DAEMON_CLIENT) {
                printf("\n======================================================\n");
                printf(" [DISCONNECTED] Connection to server lost!\n");
                printf(" Restoring local keyboard and mouse control...\n");
                printf("======================================================\n");
                fflush(stdout);
                dev_cleanup_all();
                d_ctx->exit = true;
            } else if (d_ctx->type == DAEMON_SERVER) {
                printf("\n[SERVER] Client disconnected. Waiting for new connection...\n");
                fflush(stdout);
                dev_release_virt_keys_all(d_ctx->ep_ctx, &d_ctx->device_ctx->devices);
            }
            break;
        }
        default: ERR_GOTO(err, "err: unknown fd_type\n");
    }
    CHECK(e_epoll_close_remove_simple(d_ctx->ep_ctx, stream) == -1);
    return 0;
err:
    ERR_LOG("d_epoll_close_remove");
    return -1;
}

int e_epoll_close_remove_simple(struct epoll_context* ep_ctx, struct epoll_stream* stream) {
#ifndef _WIN32
    if (stream->fd != -1) {
        epoll_ctl(ep_ctx->epoll_fd, EPOLL_CTL_DEL, stream->fd, NULL);
    }
#endif
    ssize_t index = u_array_find_index(&ep_ctx->stream_ptrs.r, &stream);
    CHECK(index == -1);
    CHECK(u_array_remove(&ep_ctx->stream_ptrs.r, (size_t)index) == -1);
    CHECK(stream_close(stream) == -1);
    u_safe_free((void**)&stream);
    return 0;
err:
    if (stream && stream->fd != RCN_INVALID_SOCKET)
        rcn_close_socket(stream->fd);
    ERR_LOG("e_epoll_close_remove_simple");
    return -1;
}

int e_epoll_wait(struct epoll_context* ep_ctx, struct epoll_event* events, int maxevents) {
#ifndef _WIN32
    return epoll_wait(ep_ctx->epoll_fd, events, maxevents, -1);
#else
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_HOTKEY) {
            fprintf(stderr, "\n======================================================\n");
            fprintf(stderr, " [EMERGENCY STOP] Global Hotkey (Ctrl+Alt+Esc) Pressed!\n");
            fprintf(stderr, " Terminating rcn immediately...\n");
            fprintf(stderr, "======================================================\n");
            fflush(stderr);
            MessageBeep(MB_ICONWARNING);
            dev_cleanup_all();
            exit(0);
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    size_t count = ep_ctx->stream_ptrs.r.length;
    if (count == 0) {
        Sleep(10);
        return 0;
    }

    WSAPOLLFD* pfds = (WSAPOLLFD*)calloc(count, sizeof(WSAPOLLFD));
    if (pfds == NULL) return -1;

    for (size_t i = 0; i < count; i++) {
        struct epoll_stream* stream = NULL;
        u_array_getv(&ep_ctx->stream_ptrs.r, (void*)&stream, i);
        pfds[i].fd = stream->fd;
        pfds[i].events = POLLIN;
        if (stream->next && stream->next->op == STREAM_OP_WRITING) {
            pfds[i].events |= POLLOUT;
        }
    }

    int res = WSAPoll(pfds, (ULONG)count, 10);
    if (res <= 0) {
        free(pfds);
        return (res == 0) ? 0 : -1;
    }

    int nfds = 0;
    for (size_t i = 0; i < count && nfds < maxevents; i++) {
        if (pfds[i].revents != 0) {
            struct epoll_stream* stream = NULL;
            u_array_getv(&ep_ctx->stream_ptrs.r, (void*)&stream, i);
            events[nfds].data.ptr = stream;
            events[nfds].events = 0;
            if (pfds[i].revents & POLLIN)
                events[nfds].events |= EPOLLIN;
            if (pfds[i].revents & POLLOUT)
                events[nfds].events |= EPOLLOUT;
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                events[nfds].events |= EPOLLHUP;
            nfds++;
        }
    }
    free(pfds);
    return nfds;
#endif
}