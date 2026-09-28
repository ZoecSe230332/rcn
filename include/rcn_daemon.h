#ifndef RCN_RCN_DAEMON_H
#define RCN_RCN_DAEMON_H

#include "rcn_peer.h"
#include "rcn_device.h"
#include "rcn_relay.h"

struct d_context {
    struct epoll_context* ep_ctx;
    struct peer_context* peer_ctx;
    struct device_context* device_ctx;
    struct relay_context* relay_ctx;
    enum rcn_state state;
    enum daemon_type type;
    bool exit;
};

struct daemon_arg {
    int port;
    char* host;
    char_arr* devices_arg;
    enum daemon_type type;
};

/* rcn_daemon.c */
int d_init_dir(void);
int d_init_log(enum daemon_type d_type);
int d_print_log(enum daemon_type d_type);
int d_init(struct daemon_arg arg);
int daemon_start(struct daemon_arg d_arg, struct relay_arg r_arg);

#endif //RCN_RCN_DAEMON_H