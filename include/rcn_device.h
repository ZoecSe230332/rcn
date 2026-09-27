#ifndef RCN_RCN_DEV_H
#define RCN_RCN_DEV_H

#include "rcn_types.h"
#include <stdint.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#define HAS_BIT(array, bit) (((array)[(bit)/8] >> ((bit)%8)) & 1)

#define MAX_EVT_BYTES   ((EV_MAX+7) / 8)
#define MAX_KEY_BYTES   ((KEY_MAX+7) / 8)
#define MAX_ABS_BYTES   ((ABS_MAX+7) / 8)
#define MAX_REL_BYTES   ((REL_MAX+7) / 8)
#define MAX_PROP_BYTES  ((INPUT_PROP_MAX+7) / 8)

/* forward declarations */
struct d_context;
struct peer_msg_event;
struct peer_context;

struct device_info {
    uint8_t evtbit[MAX_EVT_BYTES];
    uint8_t keybit[MAX_KEY_BYTES];
    uint8_t absbit[MAX_ABS_BYTES];
    uint8_t relbit[MAX_REL_BYTES];
    uint8_t propbit[MAX_PROP_BYTES];
    char name[UINPUT_MAX_NAME_SIZE];
    struct input_absinfo absinfo[ABS_MAX+1];
    struct input_id dev_id;
    size_t random_id;
};

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
int dev_get_device_info(int dev_fd, struct device* dev);
int dev_init_udev(struct epoll_context* ep_ctx, device_arr* devices, struct device_info* info_template);
int dev_close_dev(struct device_context* dev_ctx, struct epoll_stream* stream);
int dev_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item);
int dev_emit_event_msg(struct d_context* d_ctx, struct peer_msg_event event);
int dev_release_virt_keys(struct epoll_context* ep_ctx, struct device* device);
int dev_release_virt_keys_all(struct epoll_context* ep_ctx, device_arr* devices);
int dev_ctrl_devices(struct d_context* d_ctx, device_arr* devices, enum device_ctrl ctrl);

#endif //RCN_RCN_DEV_H
