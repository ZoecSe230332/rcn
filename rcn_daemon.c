#include "include/rcn.h"
#include "include/rcn_arg.h"
#include "include/rcn_daemon.h"
#include "include/rcn_device.h"
#include "include/rcn_epoll.h"
#include "include/rcn_peer.h"
#include "include/rcn_relay.h"
#include "include/rcn_stream.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static int accept_isock(struct epoll_context* ep_ctx, struct peer_context* p_ctx, const struct epoll_stream* stream) {
    struct sockaddr_in p_iaddr = { 0 };
    struct sockaddr* addr = (struct sockaddr*)&p_iaddr;
    socklen_t addr_len = sizeof(p_iaddr);
    rcn_socket_t sock_fd = accept(stream->fd, addr, &addr_len);
    CHECK(sock_fd == RCN_INVALID_SOCKET);
    if (p_ctx->peer_state == PEER_CONNECTED) {
        rcn_close_socket(sock_fd);
        return 0;
    }
    const int no_delay = 1;
    setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&no_delay, sizeof(no_delay));
    CHECK(rcn_set_nonblocking(sock_fd) == -1);
    CHECK(e_epoll_add_getr(ep_ctx, sock_fd, FD_PEER, &p_ctx->peer_stream) == -1);
    p_ctx->peer_state = PEER_CONNECTED;
    return 0;
err:
    ERR_LOG("accept_isock");
    return -1;
}

static int accept_usock(struct epoll_context* ep_ctx, epoll_stream_arr* relay_streams, const struct epoll_stream* stream) {
    struct sockaddr_un r_uaddr = { 0 };
    struct sockaddr* addr = (struct sockaddr*)&r_uaddr;
    socklen_t addr_len = sizeof(r_uaddr);
    rcn_socket_t sock_fd = accept(stream->fd, addr, &addr_len);
    CHECK(sock_fd == RCN_INVALID_SOCKET);
    CHECK(rcn_set_nonblocking(sock_fd) == -1);
    struct epoll_stream* relay_stream = NULL;
    CHECK(e_epoll_add_getr(ep_ctx, sock_fd, FD_RELAY, &relay_stream) == -1);
    CHECK(u_array_add(&relay_streams->r, &relay_stream) == -1);
    return 0;
err:
    ERR_LOG("accept_usock");
    return -1;
}

int d_print_log(enum daemon_type d_type) {
    const char* log = (d_type == DAEMON_SERVER) ? RCN_SERVER_LOG_PATH : RCN_CLIENT_LOG_PATH;
    FILE* log_file = fopen(log, "r");
    CHECK(log_file == NULL);
    char buffer[256] = { 0 };
    size_t bytes_read = 0;
    while ((bytes_read = fread(buffer, sizeof(char), sizeof(buffer), log_file)) > 0)
        fwrite(buffer, sizeof(char), bytes_read, stdout);
    CHECK(ferror(log_file) > 0);
    fclose(log_file);
    return 0;
err:
    ERR_LOG("d_print_log");
    return -1;
}

int d_init_dir(void) {
    const int rcn_dir = rcn_mkdir(RCN_DAEMON_DIR_PATH);
    CHECK(rcn_dir == -1 && errno != EEXIST);
    return 0;
err:
    ERR_LOG("d_init_dir");
    return -1;
}

int d_init_log(enum daemon_type d_type) {
    const char* log_path = (d_type == DAEMON_SERVER) ? RCN_SERVER_LOG_PATH : RCN_CLIENT_LOG_PATH;
    CHECK(log_path == NULL);
#ifndef _WIN32
    CHECK(freopen(log_path, "w", stdout) == NULL);
    CHECK(freopen(log_path, "a", stderr) == NULL);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#else
    freopen(log_path, "w", stdout);
    freopen(log_path, "a", stderr);
    if (stdout) setvbuf(stdout, NULL, _IONBF, 0);
    if (stderr) setvbuf(stderr, NULL, _IONBF, 0);
#endif
    return 0;
err:
    ERR_LOG("d_init_log");
    return -1;
}

static int resolve_host(const int port, const char *host, struct addrinfo** addr) {
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int res = getaddrinfo(host, port_str, &hints, addr);
    if (res != 0)
        goto err;
    return 0;
err:
    ERR_LOG("%s", gai_strerror(res));
    return -1;
}

static int init_peer_sock(const int port, const char *host) {
    const rcn_socket_t isock_fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(isock_fd == RCN_INVALID_SOCKET);

#ifndef _WIN32
    const int timeout_ms = 5000;
    setsockopt(isock_fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
#endif
    const int no_delay = 1;
    setsockopt(isock_fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&no_delay, sizeof(no_delay));

    struct addrinfo* p_info = NULL;
    CHECK(resolve_host(port, host, &p_info) == -1);
    bool connected = false;
    for (struct addrinfo* i = p_info; i != NULL; i = i->ai_next) {
        int res = connect(isock_fd, i->ai_addr, (int)i->ai_addrlen);
        if (res == 0) {
            connected = true;
            break;
        }
    }
    freeaddrinfo(p_info);
    CHECK(connected == false);
    CHECK(rcn_set_nonblocking(isock_fd) == -1);
    return (int)isock_fd;
err:
    if (isock_fd != RCN_INVALID_SOCKET)
        rcn_close_socket(isock_fd);
    ERR_LOG("client init_psock");
    return -1;
}

static int init_inet_sock(const int port) {
    const rcn_socket_t isock_fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(isock_fd == RCN_INVALID_SOCKET);
    const int reuse = 1;
    setsockopt(isock_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    CHECK(rcn_set_nonblocking(isock_fd) == -1);

    struct sockaddr_in s_iaddr = { 0 };
    s_iaddr.sin_family = AF_INET;
    s_iaddr.sin_port = htons((uint16_t)port);
    s_iaddr.sin_addr.s_addr = INADDR_ANY;
    CHECK(bind(isock_fd, (struct sockaddr*)&s_iaddr, sizeof(s_iaddr)) == -1);
    CHECK(listen(isock_fd, SOMAXCONN) == -1);
    return (int)isock_fd;
err:
    if (isock_fd != RCN_INVALID_SOCKET)
        rcn_close_socket(isock_fd);
    ERR_LOG("server init_psock");
    return -1;
}

static int can_exit(struct d_context* d_ctx) {
    static int exit_ticks = 0;
    if (d_ctx->exit == false)
        return 0;
    if (d_ctx->relay_ctx->relay_streams.r.length == 0 || ++exit_ticks >= 3)
        return 1;
    return 0;
}

static int dispatch_epoll(struct d_context* d_ctx, struct epoll_event* epoll_buff, size_t fd_count) {
    struct epoll_context* ep_ctx = d_ctx->ep_ctx;
    int nfds = e_epoll_wait(ep_ctx, epoll_buff, (int)fd_count);
    if (nfds <= 0)
        return nfds;
    for (int i = 0; i < nfds; i++) {
        struct epoll_event evt = epoll_buff[i];
        struct epoll_stream* stream = (struct epoll_stream*)evt.data.ptr;
        switch (stream->fd_type) {
            case FD_USOCK: {
                CHECK(accept_usock(ep_ctx, &d_ctx->relay_ctx->relay_streams, stream) == -1);
                break;
            }
            case FD_ISOCK: {
                CHECK(accept_isock(ep_ctx, d_ctx->peer_ctx, stream) == -1);
                break;
            }
            case FD_PEER:
            case FD_RELAY:
            case FD_DEV:
            case FD_UDEV: {
                CHECK(stream_stream(stream) == -1);
                break;
            }
        }
    }
    return nfds;
err:
    ERR_LOG("dispatch_epoll");
    return -1;
}

static int resolve_fd_streams(struct d_context* d_ctx, struct epoll_event* epoll_buff, int fd_count) {
    for (int i = 0; i < fd_count; i++) {
        struct epoll_event evt = epoll_buff[i];
        struct epoll_stream* stream = (struct epoll_stream*)evt.data.ptr;
        struct stream_item* stream_item = stream->next;
        if (stream_item->state == STREAM_STREAMING_CLOSED) {
            CHECK(e_epoll_close_remove(d_ctx, stream) == -1);
            continue;
        }
        if (stream_item->state != STREAM_STREAMING_COMPLETE) {
            continue;
        }
        CHECK(stream_collect(stream, &stream_item) == -1);
        if (stream_item->op != stream->default_op) {
            CHECK(e_epoll_reset_stream(d_ctx->ep_ctx, stream) == -1);
            continue;
        }
        switch (stream->fd_type) {
            case FD_PEER: {
                CHECK(p_handler(d_ctx, stream, stream_item) == -1);
                break;
            }
            case FD_RELAY: {
                CHECK(r_handler(d_ctx, stream, stream_item) == -1);
                break;
            }
            case FD_DEV: {
                CHECK(dev_handler(d_ctx, stream, stream_item) == -1);
                break;
            }
            case FD_UDEV: break;
            case FD_ISOCK: break;
            case FD_USOCK: break;
            default: ERR_GOTO(err, "err: invalid entry-type\n");
        }
        CHECK(e_epoll_reset_stream(d_ctx->ep_ctx, stream) == -1);
    }
    return 0;
err:
    ERR_LOG("resolve_fd_streams");
    return -1;
}

static int cleanup(struct d_context* d_ctx) {
    struct epoll_context* ep_ctx = d_ctx->ep_ctx;
    CHECK(r_close_relay_ctx(ep_ctx, d_ctx->relay_ctx) == -1);
    CHECK(p_close_peer_ctx(ep_ctx, d_ctx->peer_ctx) == -1);
    CHECK(dev_close_device_ctx(ep_ctx, d_ctx->device_ctx) == -1);
    CHECK(e_close_epoll_ctx(ep_ctx) == -1);
    return 0;
err:
    ERR_LOG("cleanup");
    return -1;
}

static int d_loop(struct d_context* d_ctx) {
    struct epoll_context* ep_ctx = d_ctx->ep_ctx;
    while (can_exit(d_ctx) == false) {
        const size_t fd_count = ep_ctx->stream_ptrs.r.length;
        if (fd_count == 0) {
            rcn_sleep_ms(10);
            continue;
        }
        struct epoll_event* epoll_buff = (struct epoll_event*)calloc(fd_count, sizeof(struct epoll_event));
        if (epoll_buff == NULL) return -1;
        const int nfds = dispatch_epoll(d_ctx, epoll_buff, fd_count);
        if (nfds > 0) {
            int res = resolve_fd_streams(d_ctx, epoll_buff, nfds);
            if (res == -1) {
                free(epoll_buff);
                goto err;
            }
        }
        free(epoll_buff);
    }
    return 0;
err:
    ERR_LOG("d_loop");
    return -1;
}

int d_init(struct daemon_arg arg) {
#ifndef _WIN32
    pid_t relay_pid = getppid();
    if (arg.type == DAEMON_SERVER) {
        prctl(PR_SET_NAME, RCN_PROC_NAME_SERVER, 0UL, 0UL, 0UL);
    } else {
        prctl(PR_SET_NAME, RCN_PROC_NAME_CLIENT, 0UL, 0UL, 0UL);
    }
#endif
    CHECK(d_init_dir() == -1);
    CHECK(d_init_log(arg.type) == -1);

    struct epoll_context ep_ctx = { 0 };
    struct relay_context relay_ctx = { 0 };
    struct peer_context peer_ctx = { 0 };
    struct device_context device_ctx = { 0 };

    CHECK(e_init_epoll_ctx(&ep_ctx) == -1);
    CHECK(dev_init_device_ctx(&device_ctx) == -1);

    const char* sock_path = (arg.type == DAEMON_SERVER) ? RCN_SERVER_SOCKET_PATH : RCN_CLIENT_SOCKET_PATH;
    size_t sock_len = (arg.type == DAEMON_SERVER) ? RCN_SERVER_SOCKET_LEN : RCN_CLIENT_SOCKET_LEN;
    const rcn_socket_t usock_fd = r_init_usock(sock_path, sock_len);
    CHECK(usock_fd == RCN_INVALID_SOCKET);
    CHECK(r_init_relay_ctx(&ep_ctx, &relay_ctx, usock_fd) == -1);

    rcn_socket_t net_fd = RCN_INVALID_SOCKET;
    if (arg.type == DAEMON_SERVER)
        net_fd = init_inet_sock(arg.port);
    else
        net_fd = init_peer_sock(arg.port, arg.host);
    CHECK(net_fd == RCN_INVALID_SOCKET);
    CHECK(p_init_peer_ctx(&ep_ctx, &peer_ctx, net_fd, arg.type) == -1);

    struct d_context d_ctx = { 0 };
    d_ctx.ep_ctx = &ep_ctx;
    d_ctx.peer_ctx = &peer_ctx;
    d_ctx.relay_ctx = &relay_ctx;
    d_ctx.device_ctx = &device_ctx;
    d_ctx.type = arg.type;

    if (arg.type == DAEMON_CLIENT) {
        CHECK(dev_init_devices_arg(&ep_ctx, &device_ctx, &peer_ctx, arg.devices_arg) == -1);
        CHECK(dev_ctrl_devices(&d_ctx, &device_ctx.devices, DEV_CTRL_CAPTURE) == -1);
        if (arg.devices_arg != NULL)
            CHECK(u_array_free(&arg.devices_arg->r) == -1);
    }

#ifndef _WIN32
    kill(relay_pid, SIGCONT);
#else
    HANDLE hReadyEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, (arg.type == DAEMON_SERVER) ? "rcn_server_ready" : "rcn_client_ready");
    if (hReadyEvent) {
        SetEvent(hReadyEvent);
        CloseHandle(hReadyEvent);
    }
#endif
    printf("rcn: daemon started\n");
    if (arg.type == DAEMON_SERVER) {
        u_print_server_info(arg.port);
    }
    fflush(stdout);
    CHECK(d_loop(&d_ctx) == -1);
    CHECK(cleanup(&d_ctx) == -1);
    return 0;
err:
#ifndef _WIN32
    kill(relay_pid, SIGTSTP);
#endif
    ERR_LOG("d_init");
    return -1;
}

int daemon_start(struct daemon_arg d_arg, struct relay_arg r_arg) {
#ifndef _WIN32
    const pid_t pid = fork();
    if (pid == 0)
        CHECK(d_init(d_arg) == -1);
    else
        CHECK(relay_start(r_arg) == -1);
    return 0;
err:
    ERR_LOG("daemon_start");
    return -1;
#else
    HANDLE hReadyEvent = CreateEventA(NULL, FALSE, FALSE, (d_arg.type == DAEMON_SERVER) ? "rcn_server_ready" : "rcn_client_ready");

    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    char cmd_line[2048];
    if (d_arg.type == DAEMON_SERVER) {
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s -p %d", exe_path, ARG_ACTION_WORKER_SERVER, d_arg.port);
    } else {
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s -s %s -p %d", exe_path, ARG_ACTION_WORKER_CLIENT, d_arg.host, d_arg.port);
        if (d_arg.devices_arg && d_arg.devices_arg->r.length > 0) {
            strncat(cmd_line, " -d", sizeof(cmd_line) - strlen(cmd_line) - 1);
            for (size_t i = 0; i < d_arg.devices_arg->r.length; i++) {
                char* dev_path = NULL;
                u_array_getv(&d_arg.devices_arg->r, &dev_path, i);
                strncat(cmd_line, " ", sizeof(cmd_line) - strlen(cmd_line) - 1);
                strncat(cmd_line, dev_path, sizeof(cmd_line) - strlen(cmd_line) - 1);
            }
        }
    }

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = { 0 };
    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
    BOOL created = CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi);
    if (!created) {
        flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
        created = CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi);
    }
    if (!created) {
        if (hReadyEvent) CloseHandle(hReadyEvent);
        ERR_GOTO(err, "err: CreateProcess failed (%lu)\n", GetLastError());
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    int res = relay_start(r_arg);
    if (hReadyEvent) CloseHandle(hReadyEvent);
    CHECK(res == -1);
    return 0;
err:
    ERR_LOG("daemon_start");
    return -1;
#endif
}