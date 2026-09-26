#include "include/rcn.h"
#include "include/rcn_epoll.h"
#include "include/rcn_stream.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/prctl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int accept_isock(struct epoll_context* ep_ctx, struct peer_context* p_ctx, const struct epoll_stream* stream) {
    struct sockaddr_in p_iaddr = {};
    struct sockaddr* addr = (struct sockaddr*)&p_iaddr;
    socklen_t addr_len = sizeof(p_iaddr);
    int sock_fd = TRY(accept(stream->fd, addr, &addr_len), -1);
    if (p_ctx->peer_state == PEER_CONNECTED) {
        close(sock_fd);
        return 0;
    }
    const int no_delay = 1;
    CHECK(setsockopt(sock_fd, SOL_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) == -1);
    CHECK(fcntl(sock_fd, F_SETFL, O_NONBLOCK) == -1);
    CHECK(e_epoll_add_getr(ep_ctx, sock_fd, FD_PEER, &p_ctx->peer_stream) == -1);
    p_ctx->peer_state = PEER_CONNECTED;
    return 0;
err:
    ERR_LOG("accept_isock");
    return -1;
}

static int accept_usock(struct epoll_context* ep_ctx, epoll_stream_arr* relay_streams, const struct epoll_stream* stream) {
    struct sockaddr_un r_uaddr = {};
    struct sockaddr* addr = (struct sockaddr*)&r_uaddr;
    socklen_t addr_len = sizeof(r_uaddr);
    int sock_fd = TRY(accept(stream->fd, addr, &addr_len), -1);
    CHECK(fcntl(sock_fd, F_SETFL, O_NONBLOCK) == -1);
    struct epoll_stream* relay_stream;
    CHECK(e_epoll_add_getr(ep_ctx, sock_fd, FD_RELAY, &relay_stream) == -1);
    CHECK(u_array_add(&relay_streams->r, &relay_stream) == -1);
    return 0;
err:
    ERR_LOG("accept_usock");
    return -1;
}

int d_print_log(enum daemon_type d_type) {
    char* log = NULL;
    if (d_type == DAEMON_SERVER)
        log = RCN_SERVER_LOG_PATH;
    else if (d_type == DAEMON_CLIENT)
        log = RCN_CLIENT_LOG_PATH;
    FILE* log_file = TRY(fopen(log, "r"), NULL);
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

int d_init_dir() {
    const int rcn_dir = mkdir(RCN_DAEMON_DIR_PATH, 0755);
    CHECK(rcn_dir == -1 && errno != EEXIST);
    return 0;
err:
    ERR_LOG("d_init_dir");
    return -1;
}

int d_init_log(enum daemon_type d_type) {
    char* log_path = NULL;
    if (d_type == DAEMON_SERVER)
        log_path = RCN_SERVER_LOG_PATH;
    else if (d_type == DAEMON_CLIENT)
        log_path = RCN_CLIENT_LOG_PATH;
    CHECK(log_path == NULL);
    CHECK(freopen(log_path, "w", stdout) == NULL);
    CHECK(freopen(log_path, "a", stderr) == NULL);
    CHECK(setvbuf(stdout, NULL, _IONBF, 0) != 0);
    CHECK(setvbuf(stderr, NULL, _IONBF, 0) != 0);
    return 0;
err:
    ERR_LOG("d_init_log");
    return -1;
}

static int resolve_host(const int port, const char *host, struct addrinfo** addr) {
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = {};
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
    const int isock_fd = TRY(socket(AF_INET, SOCK_STREAM, 0), -1);
    const int timeout_ms = 5000;
    CHECK(setsockopt(isock_fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_ms, sizeof(timeout_ms)) == -1);
    const int no_delay = 1;
    CHECK(setsockopt(isock_fd, SOL_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) == -1);
    struct addrinfo* p_info = NULL;
    CHECK(resolve_host(port, host, &p_info) == -1);
    bool connected = false;
    for (struct addrinfo* i = p_info; i != NULL; i = i->ai_next) {
        int res = connect(isock_fd, i->ai_addr, i->ai_addrlen);
        if (res == 0) {
            connected = true;
            break;
        }
        CHECK(errno != ETIMEDOUT);
        printf("rcn: timeout\n");
    }
    freeaddrinfo(p_info);
    CHECK(connected == false);
    int fd_flags = TRY(fcntl(isock_fd, F_GETFL, 0), 1);
    CHECK(fcntl(isock_fd, F_SETFL, O_NONBLOCK | fd_flags) == -1);
    return isock_fd;
err:
    ERR_LOG("client init_psock");
    return -1;
}

static int init_inet_sock(const int port) {
    const int isock_fd = TRY(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0), -1);
    const int reuse = 1;
    CHECK(setsockopt(isock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1);
    struct sockaddr_in s_iaddr = {};
    s_iaddr.sin_family = AF_INET;
    s_iaddr.sin_port = htons(port);
    s_iaddr.sin_addr.s_addr = INADDR_ANY;
    CHECK(bind(isock_fd, (struct sockaddr*)&s_iaddr, sizeof(s_iaddr)) == -1);
    CHECK(listen(isock_fd, 0) == -1);
    return isock_fd;
err:
    ERR_LOG("server init_psock");
    return -1;
}

static int can_exit(struct d_context* d_ctx) {
    if (d_ctx->exit == false)
        return 0;
    if (d_ctx->relay_ctx->relay_streams.r.length > 0)
        return 0;
    return 1;
}

static int dispatch_epoll(struct d_context* d_ctx, struct epoll_event* epoll_buff, size_t fd_count) {
    struct epoll_context* ep_ctx = d_ctx->ep_ctx;
    int nfds = TRY(epoll_wait(ep_ctx->epoll_fd, epoll_buff, fd_count, -1), -1);
    for (int i = 0; i < nfds; i++) {
        struct epoll_event evt = epoll_buff[i];
        struct epoll_stream* stream = evt.data.ptr;
        switch (stream->fd_type) {
            case FD_USOCK: {
                CHECK(accept_usock(ep_ctx, &d_ctx->relay_ctx->relay_streams, stream) == -1);
                break;
            }
            case FD_ISOCK: {
                CHECK(accept_isock(ep_ctx, d_ctx->peer_ctx, stream) == -1);
                break;
            }
            case FD_PEER: {
                CHECK(stream_stream(stream) == -1);
                break;
            }
            case FD_RELAY: {
                CHECK(stream_stream(stream) == -1);
                break;
            }
            case FD_DEV: {
                CHECK(stream_stream(stream) == -1);
                break;
            }
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
        struct epoll_stream* stream = evt.data.ptr;
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
            case FD_ISOCK: return 0;
            case FD_USOCK: return 0;
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
    CHECK(e_close_epoll_ctx(ep_ctx) == -1); // close epoll_ctx last, as other ctx depend on its FD
    return 0;
err:
    ERR_LOG("cleanup");
    return -1;
}

static int d_loop(struct d_context* d_ctx) {
    struct epoll_context* ep_ctx = d_ctx->ep_ctx;
    while (can_exit(d_ctx) == false) {
        const size_t fd_count = ep_ctx->stream_ptrs.r.length;
        struct epoll_event epoll_buff[fd_count];
        const int nfds = TRY(dispatch_epoll(d_ctx, epoll_buff, fd_count), -1);
        CHECK(resolve_fd_streams(d_ctx, epoll_buff, nfds) == -1);
    }
    return 0;
err:
    ERR_LOG("d_loop");
    return -1;
}

static int d_init(struct daemon_arg arg) {
    pid_t relay_pid = getppid();
    if (arg.type == DAEMON_SERVER) {
        CHECK(prctl(PR_SET_NAME, RCN_PROC_NAME_SERVER, 0UL, 0UL, 0UL) == -1);
    } else {
        CHECK(prctl(PR_SET_NAME, RCN_PROC_NAME_CLIENT, 0UL, 0UL, 0UL) == -1);
    }
    CHECK(d_init_dir() == -1);
    CHECK(d_init_log(arg.type) == -1);

    struct epoll_context ep_ctx = {};
    struct relay_context relay_ctx = {};
    struct peer_context peer_ctx = {};
    struct device_context device_ctx = {};

    CHECK(e_init_epoll_ctx(&ep_ctx) == -1);
    CHECK(dev_init_device_ctx(&device_ctx) == -1);

    char* sock_path;
    size_t sock_len;
    if (arg.type == DAEMON_SERVER) {
        sock_path = RCN_SERVER_SOCKET_PATH;
        sock_len = RCN_SERVER_SOCKET_LEN;
    } else {
        sock_path = RCN_CLIENT_SOCKET_PATH;
        sock_len = RCN_CLIENT_SOCKET_LEN;
    }
    const int usock_fd = TRY(r_init_usock(sock_path, sock_len), -1);
    CHECK(r_init_relay_ctx(&ep_ctx, &relay_ctx, usock_fd) == -1);

    int net_fd = 0;
    if (arg.type == DAEMON_SERVER)
        net_fd = TRY(init_inet_sock(arg.port), -1);
    else
        net_fd = TRY(init_peer_sock(arg.port, arg.host), -1);
    CHECK(p_init_peer_ctx(&ep_ctx, &peer_ctx, net_fd, arg.type) == -1);

    struct d_context d_ctx = {};
    d_ctx.ep_ctx = &ep_ctx;
    d_ctx.peer_ctx = &peer_ctx;
    d_ctx.relay_ctx = &relay_ctx;
    d_ctx.device_ctx = &device_ctx;
    d_ctx.type = arg.type;

    if (arg.type == DAEMON_CLIENT) {
        CHECK(dev_init_devices_arg(&ep_ctx, &device_ctx, &peer_ctx, arg.devices_arg) == -1);
        CHECK(dev_ctrl_devices(&d_ctx, &device_ctx.device_ptrs, DEV_CTRL_CAPTURE) == -1);
        CHECK(u_array_free(&arg.devices_arg->r) == -1);
    }

    CHECK(kill(relay_pid, SIGCONT) == -1);
    CHECK(d_loop(&d_ctx) == -1);
    CHECK(cleanup(&d_ctx) == -1);
    return 0;
err:
    kill(relay_pid, SIGTSTP);
    ERR_LOG("d_init");
    return -1;
}

int daemon_start(struct daemon_arg d_arg, struct relay_arg r_arg) {
    const pid_t pid = fork();
    if (pid == 0)
        CHECK(d_init(d_arg) == -1);
    else
        CHECK(relay_start(r_arg) == -1);
    return 0;
err:
    ERR_LOG("daemon_start");
    return -1;
}