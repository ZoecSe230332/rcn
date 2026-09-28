#ifndef RCN_RCN_DEV_H
#define RCN_RCN_DEV_H

#include "rcn_types.h"
#include <stdint.h>

#ifndef _WIN32
#include <linux/uinput.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#endif

/* forward declarations */
struct d_context;
struct peer_msg_event;
struct peer_context;

struct device {
    struct device_info info;
    struct epoll_stream* stream;
};

struct device_context {
    device_arr devices;
};

enum device_ctrl {
    DEV_CTRL_CAPTURE,
    DEV_CTRL_RELEASE,
};

int dev_init_devices_arg(struct epoll_context* ep_ctx, struct device_context* dev_ctx, struct peer_context* p_ctx, char_arr* dev_paths);
int dev_init_device(struct epoll_context* ep_ctx, device_arr* devices, const char *dev_path, struct device** out_dev);
int dev_init_device_ctx(struct device_context* dev_ctx);
int dev_close_device_ctx(struct epoll_context* ep_ctx, struct device_context* dev_ctx);
int dev_grab_device_by_ptr(struct device* dev, enum device_ctrl ctrl);
int dev_get_device_info(rcn_socket_t dev_fd, struct device* dev);
int dev_init_udev(struct epoll_context* ep_ctx, device_arr* devices, struct device_info* info_template);
int dev_close_dev(struct device_context* dev_ctx, struct epoll_stream* stream);
int dev_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item);
int dev_emit_event_msg(struct d_context* d_ctx, struct peer_msg_event event);
int dev_release_virt_keys(struct epoll_context* ep_ctx, struct device* device);
int dev_release_virt_keys_all(struct epoll_context* ep_ctx, device_arr* devices);
int dev_ctrl_devices(struct d_context* d_ctx, device_arr* devices, enum device_ctrl ctrl);
void dev_cleanup_all(void);

#endif //RCN_RCN_DEV_H
