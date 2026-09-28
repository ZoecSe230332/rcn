#include "include/rcn.h"
#include "include/rcn_daemon.h"
#include "include/rcn_device.h"
#include "include/rcn_epoll.h"
#include "include/rcn_stream.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <sys/random.h>
#include <unistd.h>
#include <sys/ioctl.h>

static int has_active_key(rcn_socket_t dev_fd) {
    if (dev_fd <= 0)
        DO_GOTO(fprintf(stderr, "err: dev_fd is <= 0\n"), err);
    uint8_t keybits[MAX_KEY_BYTES] = { 0 };
    CHECK(ioctl(dev_fd, EVIOCGKEY(sizeof(keybits)), keybits) == -1);
    for (int i = 0; i < (int)sizeof(keybits); i++) {
        if (keybits[i] != 0)
            return 1;
    }
    return 0;
err:
    ERR_LOG("has_active_key");
    return -1;
}

static int has_any_active_inputs(struct device* device) {
    if (HAS_BIT(device->info.evtbit, EV_KEY)) {
        int has = has_active_key(device->stream->fd);
        CHECK(has == -1);
        if (has == 1)
            return 1;
    }
    return 0;
err:
    ERR_LOG("has_any_active_inputs");
    return -1;
}

static int drain_events(struct device* dev) {
    for (;;) {
        usleep(1000);
        int has = has_any_active_inputs(dev);
        CHECK(has == -1);
        if (has == 0)
            break;
    }
    return 0;
err:
    ERR_LOG("drain_events");
    return -1;
}

int dev_init_device(struct epoll_context* ep_ctx, device_arr* devices, const char *dev_path, struct device** out_dev) {
    struct device* tmp_dev = (struct device*)calloc(1, sizeof(struct device));
    CHECK(tmp_dev == NULL);
    int dev_fd = open(dev_path, O_RDONLY | O_NONBLOCK);
    CHECK(dev_fd == -1);
    CHECK(e_epoll_add_device(ep_ctx, dev_fd, tmp_dev, FD_DEV) == -1);
    CHECK(dev_get_device_info(dev_fd, tmp_dev) == -1);
    CHECK(u_array_add(&devices->r, tmp_dev) == -1);
    u_safe_free((void**)&tmp_dev);
    CHECK(u_array_getr(&devices->r, (void**)out_dev, devices->r.length-1) == -1);
    return 0;
err:
    *out_dev = NULL;
    ERR_LOG("client init_dev");
    return -1;
}

int dev_init_device_ctx(struct device_context* dev_ctx) {
    CHECK(u_array_init(&dev_ctx->devices.r, sizeof(struct device), RCN_STD_CAPACITY) == -1);
    return 0;
err:
    ERR_LOG("e_init_device_ctx");
    return -1;
}

int dev_init_devices_arg(struct epoll_context* ep_ctx, struct device_context* dev_ctx, struct peer_context* p_ctx, char_arr* dev_paths) {
    for (size_t i = 0; i < dev_paths->r.length; i++) {
        char* dev_path = NULL;
        CHECK(u_array_getv(&dev_paths->r, &dev_path, i) == -1);
        struct device* dev = NULL;
        CHECK(dev_init_device(ep_ctx, &dev_ctx->devices, dev_path, &dev) == -1);
        CHECK(stream_queue_writing_socket(ep_ctx, p_ctx->peer_stream, PEER_HEADER_DEV_CRT, sizeof(struct device_info), &dev->info) == -1);
    }
    return 0;
err:
    ERR_LOG("dev_init_device_arr");
    return -1;
}

int dev_close_device_ctx(struct epoll_context* ep_ctx, struct device_context* dev_ctx) {
    while (dev_ctx->devices.r.length > 0) {
        struct device* dev = NULL;
        CHECK(u_array_getr(&dev_ctx->devices.r, (void**)&dev, 0) == -1);
        CHECK(e_epoll_close_remove_simple(ep_ctx, dev->stream) == -1);
        CHECK(u_array_remove(&dev_ctx->devices.r, 0) == -1);
    }
    CHECK(u_array_free(&dev_ctx->devices.r) == -1);
    return 0;
err:
    ERR_LOG("dev_close_device_ctx");
    return -1;
}

int dev_grab_device_by_ptr(struct device* dev, enum device_ctrl ctrl) {
    CHECK(drain_events(dev) == -1);
    CHECK(ioctl(dev->stream->fd, EVIOCGRAB, ctrl == DEV_CTRL_CAPTURE) == -1);
    return 0;
err:
    ERR_LOG("grab_dev_by_ptr");
    return -1;
}

int dev_get_device_info(rcn_socket_t dev_fd, struct device* device) {
    struct device_info* info = &device->info;
    ssize_t r_res = getrandom(&device->info.random_id, sizeof(device->info.random_id), 0);
    CHECK(r_res == -1);
    CHECK(ioctl(dev_fd, EVIOCGID, &info->dev_id) == -1);
    CHECK(ioctl(dev_fd, EVIOCGNAME(sizeof(info->name)-1), info->name) == -1);
    CHECK(ioctl(dev_fd, EVIOCGBIT(0, sizeof(info->evtbit)), info->evtbit) == -1);
    CHECK(ioctl(dev_fd, EVIOCGBIT(EV_KEY, sizeof(info->keybit)), info->keybit) == -1);
    CHECK(ioctl(dev_fd, EVIOCGBIT(EV_ABS, sizeof(info->absbit)), info->absbit) == -1);
    CHECK(ioctl(dev_fd, EVIOCGBIT(EV_REL, sizeof(info->relbit)), info->relbit) == -1);
    CHECK(ioctl(dev_fd, EVIOCGPROP(sizeof(info->propbit)), info->propbit) == -1);
    if (!HAS_BIT(info->evtbit, EV_ABS))
        return 0;
    for (int i = 0; i < ABS_MAX; i++) {
        if (!HAS_BIT(info->absbit, i))
            continue;
        CHECK(ioctl(dev_fd, EVIOCGABS(i), &info->absinfo[i]) == -1);
    }
    return 0;
err:
    ERR_LOG("e_get_device_info");
    return -1;
}

static int set_udev_bits(int u_fd, unsigned long set_ioctl, uint8_t* bitmap, int max) {
    for (int i = 0; i < max; i++) {
        if (HAS_BIT(bitmap, i))
            CHECK(ioctl(u_fd, set_ioctl, i) == -1);
    }
    return 0;
err:
    ERR_LOG("set_udev_bits");
    return -1;
}

static int apply_udev_info(int u_fd, struct device* device, struct device_info* template_info) {
    CHECK(ioctl(u_fd, UI_SET_EVBIT, EV_SYN) == -1);
    CHECK(set_udev_bits(u_fd, UI_SET_PROPBIT, template_info->propbit, INPUT_PROP_MAX) == -1);
    if (HAS_BIT(template_info->evtbit, EV_KEY)) {
        CHECK(ioctl(u_fd, UI_SET_EVBIT, EV_KEY) == -1);
        CHECK(set_udev_bits(u_fd, UI_SET_KEYBIT, template_info->keybit, KEY_MAX) == -1);
    }
    if (HAS_BIT(template_info->evtbit, EV_REL)) {
        CHECK(ioctl(u_fd, UI_SET_EVBIT, EV_REL) == -1);
        CHECK(set_udev_bits(u_fd, UI_SET_RELBIT, template_info->relbit, REL_MAX) == -1);
    }
    if (HAS_BIT(template_info->evtbit, EV_ABS)) {
        CHECK(ioctl(u_fd, UI_SET_EVBIT, EV_ABS) == -1);
        for (int i = 0; i < ABS_MAX; i++) {
            if (!HAS_BIT(template_info->absbit, i))
                continue;
            CHECK(ioctl(u_fd, UI_SET_ABSBIT, i) == -1);
            struct uinput_abs_setup abs_setup = { 0 };
            abs_setup.code = i;
            abs_setup.absinfo = template_info->absinfo[i];
            CHECK(ioctl(u_fd, UI_ABS_SETUP, &abs_setup) == -1);
        }
    }
    struct uinput_setup setup = { 0 };
    setup.id = template_info->dev_id;
    char tmp_buff[UINPUT_MAX_NAME_SIZE * 2];
    snprintf(tmp_buff, sizeof(tmp_buff), "RCN-VIRT-%s", template_info->name);
    strncpy(setup.name, tmp_buff, UINPUT_MAX_NAME_SIZE);
    strncpy(template_info->name, tmp_buff, UINPUT_MAX_NAME_SIZE);
    memcpy(&device->info, template_info, sizeof(struct device_info));
    CHECK(ioctl(u_fd, UI_DEV_SETUP, &setup) == -1);
    CHECK(ioctl(u_fd, UI_DEV_CREATE) == -1);
    return 0;
err:
    ERR_LOG("apply_udev_info");
    return -1;
}

int dev_init_udev(struct epoll_context* ep_ctx, device_arr* devices, struct device_info* template_info) {
    int u_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    CHECK(u_fd == -1);
    struct device* device = (struct device*)calloc(1, sizeof(struct device));
    CHECK(device == NULL);
    CHECK(apply_udev_info(u_fd, device, template_info) == -1);
    CHECK(e_epoll_add_device(ep_ctx, u_fd, device, FD_UDEV) == -1);
    CHECK(u_array_add(&devices->r, device) == -1);
    return 0;
err:
    if (u_fd != -1)
        close(u_fd);
    ERR_LOG("e_create_udev");
    return -1;
}

int dev_emit_event_msg(struct d_context* d_ctx, struct peer_msg_event event) {
    struct device *dev = NULL;
    device_arr* devices = &d_ctx->device_ctx->devices;
    for (size_t i = 0; i < devices->r.length; i++) {
        CHECK(u_array_getr(&devices->r, (void**)&dev, i) == -1);
        if (dev->info.random_id == event.random_id)
            break;
    }
    CHECK(dev == NULL);
    CHECK(stream_queue_writing_device(d_ctx->ep_ctx, dev->stream, event.evt_data) == -1);
    if (event.evt_data.type == EV_REL) {
        struct input_event evt = { 0 };
        evt.type = EV_SYN;
        evt.code = SYN_REPORT;
        evt.value = 0;
        CHECK(stream_queue_writing_device(d_ctx->ep_ctx, dev->stream, evt) == -1);
    }
    return 0;
err:
    ERR_LOG("emit_event");
    return -1;
}

int dev_release_virt_keys(struct epoll_context* ep_ctx, struct device* device) {
    struct input_event evt = { 0 };
    if (HAS_BIT(device->info.evtbit, EV_KEY)) {
        for (int i = 0; i < KEY_MAX; i++) {
            if (!HAS_BIT(device->info.keybit, i))
                continue;
            evt.type = EV_KEY;
            evt.code = i;
            evt.value = 0;
            CHECK(stream_queue_writing_device(ep_ctx, device->stream, evt) == -1);
        }
        evt.type = EV_SYN;
        evt.code = SYN_REPORT;
        evt.value = 0;
        CHECK(stream_queue_writing_device(ep_ctx, device->stream, evt) == -1);
    }
    return 0;
err:
    ERR_LOG("dev_release_virt_keys");
    return -1;
}

int dev_release_virt_keys_all(struct epoll_context* ep_ctx, device_arr* devices) {
    for (size_t i = 0; i < devices->r.length; i++) {
        struct device* dev = NULL;
        CHECK(u_array_getr(&devices->r, (void**)&dev, i) == -1);
        CHECK(dev_release_virt_keys(ep_ctx, dev) == -1);
    }
    return 0;
err:
    ERR_LOG("dev_release_virt_keys_all");
    return -1;
}

int dev_close_dev(struct device_context* dev_ctx, struct epoll_stream* stream) {
    ssize_t index = u_array_find_index(&dev_ctx->devices.r, &stream);
    CHECK(index == -1);
    CHECK(u_array_remove(&dev_ctx->devices.r, (size_t)index) == -1);
    return 0;
err:
    ERR_LOG("e_close_dev");
    return -1;
}

static struct d_context* s_linux_d_ctx = NULL;

int dev_ctrl_devices(struct d_context* d_ctx, device_arr* devices, enum device_ctrl ctrl) {
    s_linux_d_ctx = d_ctx;
    for (size_t i = 0; i < devices->r.length; i++) {
        struct device* dev = NULL;
        CHECK(u_array_getr(&devices->r, (void**)&dev, i) == -1);
        CHECK(dev_grab_device_by_ptr(dev, ctrl) == -1);
        struct epoll_event ep_evt = { 0 };
        ep_evt.events = dev->stream->fd_type == FD_DEV ? EPOLLIN : EPOLLOUT;
        ep_evt.data.ptr = dev->stream;
        int res = 0;
        if (ctrl == DEV_CTRL_RELEASE)
            res = epoll_ctl(d_ctx->ep_ctx->epoll_fd, EPOLL_CTL_DEL, dev->stream->fd, &ep_evt);
        else if (ctrl == DEV_CTRL_CAPTURE)
            res = epoll_ctl(d_ctx->ep_ctx->epoll_fd, EPOLL_CTL_ADD, dev->stream->fd, &ep_evt);
        CHECK(res == -1 && errno != EEXIST && errno != ENOENT);
    }
    return 0;
err:
    ERR_LOG("dev_ctrl_devices");
    return -1;
}

int dev_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    bool found_dev = false;
    struct device* dev = NULL;
    for (size_t i = 0; i < d_ctx->device_ctx->devices.r.length; i++) {
        CHECK(u_array_getr(&d_ctx->device_ctx->devices.r, (void**)&dev, i) == -1);
        if (dev->stream != stream)
            continue;
        found_dev = true;
        break;
    }
    CHECK(found_dev == false);

    // Emergency stop keybind check: Ctrl + Alt + Esc / Pause
    struct input_event evt = stream_item->payload.evt;
    static bool s_l_ctrl = false;
    static bool s_l_alt = false;
    if (evt.type == EV_KEY) {
        if (evt.code == KEY_LEFTCTRL || evt.code == KEY_RIGHTCTRL) s_l_ctrl = (evt.value != 0);
        if (evt.code == KEY_LEFTALT  || evt.code == KEY_RIGHTALT)  s_l_alt  = (evt.value != 0);
        if (s_l_ctrl && s_l_alt && (evt.code == KEY_ESC || evt.code == KEY_PAUSE) && evt.value == 1) {
            fprintf(stderr, "\n======================================================\n");
            fprintf(stderr, " [EMERGENCY STOP] Hotkey (Ctrl+Alt+Esc) Triggered!\n");
            fprintf(stderr, " Releasing all grabbed devices and stopping...\n");
            fprintf(stderr, "======================================================\n");
            fflush(stderr);
            dev_ctrl_devices(d_ctx, &d_ctx->device_ctx->devices, DEV_CTRL_RELEASE);
            if (d_ctx->peer_ctx && d_ctx->peer_ctx->peer_stream && d_ctx->peer_ctx->peer_stream->fd != RCN_INVALID_SOCKET) {
                struct stream_header stop_hdr = { .size = 0, .value = PEER_HEADER_STOP };
                rcn_send(d_ctx->peer_ctx->peer_stream->fd, &stop_hdr, sizeof(stop_hdr));
                rcn_shutdown_socket(d_ctx->peer_ctx->peer_stream->fd);
                rcn_close_socket(d_ctx->peer_ctx->peer_stream->fd);
                d_ctx->peer_ctx->peer_stream->fd = RCN_INVALID_SOCKET;
            }
            exit(0);
            return 0;
        }
    }

    struct peer_msg_event msg = { 0 };
    msg.evt_data = stream_item->payload.evt;
    msg.random_id = dev->info.random_id;
    CHECK(stream_queue_writing_socket(d_ctx->ep_ctx, d_ctx->peer_ctx->peer_stream, PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &msg) == -1);
    return 0;
err:
    ERR_LOG("dev_handler");
    return -1;
}

void dev_cleanup_all(void) {
    if (s_linux_d_ctx && s_linux_d_ctx->device_ctx) {
        dev_ctrl_devices(s_linux_d_ctx, &s_linux_d_ctx->device_ctx->devices, DEV_CTRL_RELEASE);
    }
}

#else // _WIN32 Implementation

static HHOOK s_kb_hook = NULL;
static HHOOK s_mouse_hook = NULL;
static bool s_capturing = false;
static struct d_context* s_d_ctx = NULL;
static uint64_t s_kb_random_id = 0;
static uint64_t s_mouse_random_id = 0;
static POINT s_last_mouse_pos = { 0, 0 };
static bool s_has_last_mouse = false;

void dev_cleanup_all(void) {
    s_capturing = false;
    if (s_kb_hook) {
        UnhookWindowsHookEx(s_kb_hook);
        s_kb_hook = NULL;
    }
    if (s_mouse_hook) {
        UnhookWindowsHookEx(s_mouse_hook);
        s_mouse_hook = NULL;
    }
    s_has_last_mouse = false;
}

/* Keycode translation table: Linux KEY_* -> Windows VK_* */
static WORD linux_to_vk(uint16_t code) {
    switch (code) {
        case KEY_ESC: return VK_ESCAPE;
        case KEY_1: return '1'; case KEY_2: return '2'; case KEY_3: return '3';
        case KEY_4: return '4'; case KEY_5: return '5'; case KEY_6: return '6';
        case KEY_7: return '7'; case KEY_8: return '8'; case KEY_9: return '9';
        case KEY_0: return '0';
        case KEY_MINUS: return VK_OEM_MINUS;
        case KEY_EQUAL: return VK_OEM_PLUS;
        case KEY_BACKSPACE: return VK_BACK;
        case KEY_TAB: return VK_TAB;
        case KEY_Q: return 'Q'; case KEY_W: return 'W'; case KEY_E: return 'E';
        case KEY_R: return 'R'; case KEY_T: return 'T'; case KEY_Y: return 'Y';
        case KEY_U: return 'U'; case KEY_I: return 'I'; case KEY_O: return 'O';
        case KEY_P: return 'P';
        case KEY_LEFTBRACE: return VK_OEM_4;
        case KEY_RIGHTBRACE: return VK_OEM_6;
        case KEY_ENTER: return VK_RETURN;
        case KEY_LEFTCTRL: return VK_LCONTROL;
        case KEY_A: return 'A'; case KEY_S: return 'S'; case KEY_D: return 'D';
        case KEY_F: return 'F'; case KEY_G: return 'G'; case KEY_H: return 'H';
        case KEY_J: return 'J'; case KEY_K: return 'K'; case KEY_L: return 'L';
        case KEY_SEMICOLON: return VK_OEM_1;
        case KEY_APOSTROPHE: return VK_OEM_7;
        case KEY_GRAVE: return VK_OEM_3;
        case KEY_LEFTSHIFT: return VK_LSHIFT;
        case KEY_BACKSLASH: return VK_OEM_5;
        case KEY_Z: return 'Z'; case KEY_X: return 'X'; case KEY_C: return 'C';
        case KEY_V: return 'V'; case KEY_B: return 'B'; case KEY_N: return 'N';
        case KEY_M: return 'M';
        case KEY_COMMA: return VK_OEM_COMMA;
        case KEY_DOT: return VK_OEM_PERIOD;
        case KEY_SLASH: return VK_OEM_2;
        case KEY_RIGHTSHIFT: return VK_RSHIFT;
        case KEY_KPASTERISK: return VK_MULTIPLY;
        case KEY_LEFTALT: return VK_LMENU;
        case KEY_SPACE: return VK_SPACE;
        case KEY_CAPSLOCK: return VK_CAPITAL;
        case KEY_F1: return VK_F1; case KEY_F2: return VK_F2; case KEY_F3: return VK_F3;
        case KEY_F4: return VK_F4; case KEY_F5: return VK_F5; case KEY_F6: return VK_F6;
        case KEY_F7: return VK_F7; case KEY_F8: return VK_F8; case KEY_F9: return VK_F9;
        case KEY_F10: return VK_F10; case KEY_F11: return VK_F11; case KEY_F12: return VK_F12;
        case KEY_NUMLOCK: return VK_NUMLOCK;
        case KEY_SCROLLLOCK: return VK_SCROLL;
        case KEY_KP7: return VK_NUMPAD7; case KEY_KP8: return VK_NUMPAD8; case KEY_KP9: return VK_NUMPAD9;
        case KEY_KPMINUS: return VK_SUBTRACT;
        case KEY_KP4: return VK_NUMPAD4; case KEY_KP5: return VK_NUMPAD5; case KEY_KP6: return VK_NUMPAD6;
        case KEY_KPPLUS: return VK_ADD;
        case KEY_KP1: return VK_NUMPAD1; case KEY_KP2: return VK_NUMPAD2; case KEY_KP3: return VK_NUMPAD3;
        case KEY_KP0: return VK_NUMPAD0;
        case KEY_KPDOT: return VK_DECIMAL;
        case KEY_KPENTER: return VK_RETURN;
        case KEY_RIGHTCTRL: return VK_RCONTROL;
        case KEY_KPSLASH: return VK_DIVIDE;
        case KEY_RIGHTALT: return VK_RMENU;
        case KEY_HOME: return VK_HOME;
        case KEY_UP: return VK_UP;
        case KEY_PAGEUP: return VK_PRIOR;
        case KEY_LEFT: return VK_LEFT;
        case KEY_RIGHT: return VK_RIGHT;
        case KEY_END: return VK_END;
        case KEY_DOWN: return VK_DOWN;
        case KEY_PAGEDOWN: return VK_NEXT;
        case KEY_INSERT: return VK_INSERT;
        case KEY_DELETE: return VK_DELETE;
        case KEY_LEFTMETA: return VK_LWIN;
        case KEY_RIGHTMETA: return VK_RWIN;
        default: return 0;
    }
}

/* Keycode translation table: Windows VK_* -> Linux KEY_* */
static uint16_t vk_to_linux(DWORD vk) {
    if (vk >= 'A' && vk <= 'Z') {
        static const uint16_t letters[] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
            KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
            KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
        };
        return letters[vk - 'A'];
    }
    if (vk >= '1' && vk <= '9') return KEY_1 + (uint16_t)(vk - '1');
    if (vk == '0') return KEY_0;

    switch (vk) {
        case VK_ESCAPE: return KEY_ESC;
        case VK_BACK: return KEY_BACKSPACE;
        case VK_TAB: return KEY_TAB;
        case VK_RETURN: return KEY_ENTER;
        case VK_SPACE: return KEY_SPACE;
        case VK_LSHIFT: return KEY_LEFTSHIFT;
        case VK_RSHIFT: return KEY_RIGHTSHIFT;
        case VK_SHIFT: return KEY_LEFTSHIFT;
        case VK_LCONTROL: return KEY_LEFTCTRL;
        case VK_RCONTROL: return KEY_RIGHTCTRL;
        case VK_CONTROL: return KEY_LEFTCTRL;
        case VK_LMENU: return KEY_LEFTALT;
        case VK_RMENU: return KEY_RIGHTALT;
        case VK_MENU: return KEY_LEFTALT;
        case VK_LWIN: return KEY_LEFTMETA;
        case VK_RWIN: return KEY_RIGHTMETA;
        case VK_CAPITAL: return KEY_CAPSLOCK;
        case VK_UP: return KEY_UP;
        case VK_DOWN: return KEY_DOWN;
        case VK_LEFT: return KEY_LEFT;
        case VK_RIGHT: return KEY_RIGHT;
        case VK_HOME: return KEY_HOME;
        case VK_END: return KEY_END;
        case VK_PRIOR: return KEY_PAGEUP;
        case VK_NEXT: return KEY_PAGEDOWN;
        case VK_INSERT: return KEY_INSERT;
        case VK_DELETE: return KEY_DELETE;
        case VK_OEM_MINUS: return KEY_MINUS;
        case VK_OEM_PLUS: return KEY_EQUAL;
        case VK_OEM_1: return KEY_SEMICOLON;
        case VK_OEM_2: return KEY_SLASH;
        case VK_OEM_3: return KEY_GRAVE;
        case VK_OEM_4: return KEY_LEFTBRACE;
        case VK_OEM_5: return KEY_BACKSLASH;
        case VK_OEM_6: return KEY_RIGHTBRACE;
        case VK_OEM_7: return KEY_APOSTROPHE;
        case VK_OEM_COMMA: return KEY_COMMA;
        case VK_OEM_PERIOD: return KEY_DOT;
        case VK_F1: return KEY_F1; case VK_F2: return KEY_F2; case VK_F3: return KEY_F3;
        case VK_F4: return KEY_F4; case VK_F5: return KEY_F5; case VK_F6: return KEY_F6;
        case VK_F7: return KEY_F7; case VK_F8: return KEY_F8; case VK_F9: return KEY_F9;
        case VK_F10: return KEY_F10; case VK_F11: return KEY_F11; case VK_F12: return KEY_F12;
        default: return KEY_RESERVED;
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKbd = (KBDLLHOOKSTRUCT*)lParam;
        bool is_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        // Emergency Stop Keybind: Ctrl + Alt + Esc or Ctrl + Alt + Pause
        if (is_down) {
            bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool alt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            if (ctrl && alt && (pKbd->vkCode == VK_ESCAPE || pKbd->vkCode == VK_PAUSE)) {
                fprintf(stderr, "\n======================================================\n");
                fprintf(stderr, " [EMERGENCY STOP] Hotkey (Ctrl+Alt+Esc) Triggered!\n");
                fprintf(stderr, " Immediately releasing all hooks and stopping rcn...\n");
                fprintf(stderr, "======================================================\n");
                fflush(stderr);
                MessageBeep(MB_ICONWARNING);

                // 1. Immediately remove hooks to restore local control
                dev_cleanup_all();

                // 2. Notify remote server and disconnect immediately
                if (s_d_ctx && s_d_ctx->peer_ctx && s_d_ctx->peer_ctx->peer_stream &&
                    s_d_ctx->peer_ctx->peer_stream->fd != RCN_INVALID_SOCKET) {
                    struct stream_header stop_hdr = { .size = 0, .value = PEER_HEADER_STOP };
                    rcn_send(s_d_ctx->peer_ctx->peer_stream->fd, &stop_hdr, sizeof(stop_hdr));
                    rcn_shutdown_socket(s_d_ctx->peer_ctx->peer_stream->fd);
                    rcn_close_socket(s_d_ctx->peer_ctx->peer_stream->fd);
                    s_d_ctx->peer_ctx->peer_stream->fd = RCN_INVALID_SOCKET;
                }

                exit(0);
            }
        }

        if (s_capturing) {
            // Fail-safe: verify peer is connected and stream is valid
            if (!s_d_ctx || !s_d_ctx->peer_ctx || s_d_ctx->peer_ctx->peer_state != PEER_CONNECTED ||
                !s_d_ctx->peer_ctx->peer_stream || s_d_ctx->peer_ctx->peer_stream->fd == RCN_INVALID_SOCKET) {
                dev_cleanup_all();
                return CallNextHookEx(s_kb_hook, nCode, wParam, lParam);
            }

            uint16_t linux_key = vk_to_linux(pKbd->vkCode);
            if (linux_key != KEY_RESERVED) {
                struct peer_msg_event msg = { 0 };
                msg.evt_data.type = EV_KEY;
                msg.evt_data.code = linux_key;
                msg.evt_data.value = is_down ? 1 : 0;
                msg.random_id = s_kb_random_id;

                if (stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                                PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &msg) == -1) {
                    dev_cleanup_all();
                    return CallNextHookEx(s_kb_hook, nCode, wParam, lParam);
                }

                struct peer_msg_event syn_msg = { 0 };
                syn_msg.evt_data.type = EV_SYN;
                syn_msg.evt_data.code = SYN_REPORT;
                syn_msg.evt_data.value = 0;
                syn_msg.random_id = s_kb_random_id;
                stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                            PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &syn_msg);

                // Suppress local keypress when captured
                return 1;
            }
        }
    }
    return CallNextHookEx(s_kb_hook, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && s_capturing) {
        // Fail-safe: verify peer is connected and stream is valid
        if (!s_d_ctx || !s_d_ctx->peer_ctx || s_d_ctx->peer_ctx->peer_state != PEER_CONNECTED ||
            !s_d_ctx->peer_ctx->peer_stream || s_d_ctx->peer_ctx->peer_stream->fd == RCN_INVALID_SOCKET) {
            dev_cleanup_all();
            return CallNextHookEx(s_mouse_hook, nCode, wParam, lParam);
        }

        MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
        bool handled = false;

        if (wParam == WM_MOUSEMOVE) {
            if (s_has_last_mouse) {
                int dx = pMouse->pt.x - s_last_mouse_pos.x;
                int dy = pMouse->pt.y - s_last_mouse_pos.y;
                if (dx != 0 || dy != 0) {
                    if (dx != 0) {
                        struct peer_msg_event msg = { 0 };
                        msg.evt_data.type = EV_REL;
                        msg.evt_data.code = REL_X;
                        msg.evt_data.value = dx;
                        msg.random_id = s_mouse_random_id;
                        if (stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                                        PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &msg) == -1) {
                            dev_cleanup_all();
                            return CallNextHookEx(s_mouse_hook, nCode, wParam, lParam);
                        }
                    }
                    if (dy != 0) {
                        struct peer_msg_event msg = { 0 };
                        msg.evt_data.type = EV_REL;
                        msg.evt_data.code = REL_Y;
                        msg.evt_data.value = dy;
                        msg.random_id = s_mouse_random_id;
                        if (stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                                        PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &msg) == -1) {
                            dev_cleanup_all();
                            return CallNextHookEx(s_mouse_hook, nCode, wParam, lParam);
                        }
                    }
                    struct peer_msg_event syn_msg = { 0 };
                    syn_msg.evt_data.type = EV_SYN;
                    syn_msg.evt_data.code = SYN_REPORT;
                    syn_msg.evt_data.value = 0;
                    syn_msg.random_id = s_mouse_random_id;
                    stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                                PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &syn_msg);
                }
            }
            s_last_mouse_pos = pMouse->pt;
            s_has_last_mouse = true;
            handled = true;
        } else if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP ||
                   wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP ||
                   wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP) {
            uint16_t btn = BTN_LEFT;
            int val = 0;
            if (wParam == WM_LBUTTONDOWN) { btn = BTN_LEFT; val = 1; }
            else if (wParam == WM_LBUTTONUP) { btn = BTN_LEFT; val = 0; }
            else if (wParam == WM_RBUTTONDOWN) { btn = BTN_RIGHT; val = 1; }
            else if (wParam == WM_RBUTTONUP) { btn = BTN_RIGHT; val = 0; }
            else if (wParam == WM_MBUTTONDOWN) { btn = BTN_MIDDLE; val = 1; }
            else if (wParam == WM_MBUTTONUP) { btn = BTN_MIDDLE; val = 0; }

            struct peer_msg_event msg = { 0 };
            msg.evt_data.type = EV_KEY;
            msg.evt_data.code = btn;
            msg.evt_data.value = val;
            msg.random_id = s_mouse_random_id;
            if (stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                            PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &msg) == -1) {
                dev_cleanup_all();
                return CallNextHookEx(s_mouse_hook, nCode, wParam, lParam);
            }

            struct peer_msg_event syn_msg = { 0 };
            syn_msg.evt_data.type = EV_SYN;
            syn_msg.evt_data.code = SYN_REPORT;
            syn_msg.evt_data.value = 0;
            syn_msg.random_id = s_mouse_random_id;
            stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                        PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &syn_msg);
            handled = true;
        } else if (wParam == WM_MOUSEWHEEL) {
            short delta = (short)HIWORD(pMouse->mouseData);
            struct peer_msg_event msg = { 0 };
            msg.evt_data.type = EV_REL;
            msg.evt_data.code = REL_WHEEL;
            msg.evt_data.value = delta / WHEEL_DELTA;
            msg.random_id = s_mouse_random_id;
            if (stream_queue_writing_socket(s_d_ctx->ep_ctx, s_d_ctx->peer_ctx->peer_stream,
                                            PEER_HEADER_EVENT, sizeof(struct peer_msg_event), &msg) == -1) {
                dev_cleanup_all();
                return CallNextHookEx(s_mouse_hook, nCode, wParam, lParam);
            }
            handled = true;
        }

        if (handled && wParam != WM_MOUSEMOVE) {
            return 1; // Suppress local clicks when captured
        }
    }
    return CallNextHookEx(s_mouse_hook, nCode, wParam, lParam);
}

int dev_init_device_ctx(struct device_context* dev_ctx) {
    CHECK(u_array_init(&dev_ctx->devices.r, sizeof(struct device), RCN_STD_CAPACITY) == -1);
    return 0;
err:
    ERR_LOG("dev_init_device_ctx");
    return -1;
}

int dev_init_devices_arg(struct epoll_context* ep_ctx, struct device_context* dev_ctx, struct peer_context* p_ctx, char_arr* dev_paths) {
    (void)dev_paths;
    srand((unsigned int)time(NULL));

    // 1. Create virtual keyboard device description
    struct device kb_dev = { 0 };
    snprintf(kb_dev.info.name, sizeof(kb_dev.info.name), "Windows Keyboard");
    kb_dev.info.random_id = ((uint64_t)rand() << 32) | (uint64_t)rand();
    s_kb_random_id = kb_dev.info.random_id;
    SET_BIT(kb_dev.info.evtbit, EV_SYN);
    SET_BIT(kb_dev.info.evtbit, EV_KEY);
    for (int i = 0; i < KEY_MAX; i++) {
        SET_BIT(kb_dev.info.keybit, i);
    }
    CHECK(u_array_add(&dev_ctx->devices.r, &kb_dev) == -1);
    CHECK(stream_queue_writing_socket(ep_ctx, p_ctx->peer_stream, PEER_HEADER_DEV_CRT, sizeof(struct device_info), &kb_dev.info) == -1);

    // 2. Create virtual mouse device description
    struct device mouse_dev = { 0 };
    snprintf(mouse_dev.info.name, sizeof(mouse_dev.info.name), "Windows Mouse");
    mouse_dev.info.random_id = ((uint64_t)rand() << 32) | (uint64_t)rand();
    s_mouse_random_id = mouse_dev.info.random_id;
    SET_BIT(mouse_dev.info.evtbit, EV_SYN);
    SET_BIT(mouse_dev.info.evtbit, EV_KEY);
    SET_BIT(mouse_dev.info.evtbit, EV_REL);
    SET_BIT(mouse_dev.info.keybit, BTN_LEFT);
    SET_BIT(mouse_dev.info.keybit, BTN_RIGHT);
    SET_BIT(mouse_dev.info.keybit, BTN_MIDDLE);
    SET_BIT(mouse_dev.info.keybit, BTN_SIDE);
    SET_BIT(mouse_dev.info.keybit, BTN_EXTRA);
    SET_BIT(mouse_dev.info.relbit, REL_X);
    SET_BIT(mouse_dev.info.relbit, REL_Y);
    SET_BIT(mouse_dev.info.relbit, REL_WHEEL);
    SET_BIT(mouse_dev.info.relbit, REL_HWHEEL);
    CHECK(u_array_add(&dev_ctx->devices.r, &mouse_dev) == -1);
    CHECK(stream_queue_writing_socket(ep_ctx, p_ctx->peer_stream, PEER_HEADER_DEV_CRT, sizeof(struct device_info), &mouse_dev.info) == -1);

    return 0;
err:
    ERR_LOG("dev_init_devices_arg");
    return -1;
}

int dev_close_device_ctx(struct epoll_context* ep_ctx, struct device_context* dev_ctx) {
    (void)ep_ctx;
    if (s_kb_hook) { UnhookWindowsHookEx(s_kb_hook); s_kb_hook = NULL; }
    if (s_mouse_hook) { UnhookWindowsHookEx(s_mouse_hook); s_mouse_hook = NULL; }
    s_capturing = false;
    CHECK(u_array_free(&dev_ctx->devices.r) == -1);
    return 0;
err:
    ERR_LOG("dev_close_device_ctx");
    return -1;
}

int dev_init_udev(struct epoll_context* ep_ctx, device_arr* devices, struct device_info* template_info) {
    (void)ep_ctx;
    struct device dev = { 0 };
    memcpy(&dev.info, template_info, sizeof(struct device_info));
    CHECK(u_array_add(&devices->r, &dev) == -1);
    printf("rcn: registered virtual device '%s' (id: %llu)\n", template_info->name, (unsigned long long)template_info->random_id);
    return 0;
err:
    ERR_LOG("dev_init_udev");
    return -1;
}

int dev_emit_event_msg(struct d_context* d_ctx, struct peer_msg_event event) {
    (void)d_ctx;
    INPUT input = { 0 };

    if (event.evt_data.type == EV_REL) {
        input.type = INPUT_MOUSE;
        if (event.evt_data.code == REL_X) {
            input.mi.dx = (LONG)event.evt_data.value;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
        } else if (event.evt_data.code == REL_Y) {
            input.mi.dy = (LONG)event.evt_data.value;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
        } else if (event.evt_data.code == REL_WHEEL) {
            input.mi.mouseData = (DWORD)(event.evt_data.value * WHEEL_DELTA);
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        } else if (event.evt_data.code == REL_HWHEEL) {
            input.mi.mouseData = (DWORD)(event.evt_data.value * WHEEL_DELTA);
            input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        }
        if (input.mi.dwFlags != 0) {
            SendInput(1, &input, sizeof(INPUT));
        }
    } else if (event.evt_data.type == EV_KEY) {
        if (event.evt_data.code >= BTN_MISC && event.evt_data.code <= BTN_EXTRA) {
            input.type = INPUT_MOUSE;
            DWORD down_flag = 0, up_flag = 0;
            switch (event.evt_data.code) {
                case BTN_LEFT: down_flag = MOUSEEVENTF_LEFTDOWN; up_flag = MOUSEEVENTF_LEFTUP; break;
                case BTN_RIGHT: down_flag = MOUSEEVENTF_RIGHTDOWN; up_flag = MOUSEEVENTF_RIGHTUP; break;
                case BTN_MIDDLE: down_flag = MOUSEEVENTF_MIDDLEDOWN; up_flag = MOUSEEVENTF_MIDDLEUP; break;
                case BTN_SIDE: down_flag = MOUSEEVENTF_XDOWN; up_flag = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON1; break;
                case BTN_EXTRA: down_flag = MOUSEEVENTF_XDOWN; up_flag = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON2; break;
            }
            input.mi.dwFlags = (event.evt_data.value != 0) ? down_flag : up_flag;
            if (input.mi.dwFlags != 0) {
                SendInput(1, &input, sizeof(INPUT));
            }
        } else {
            WORD vk = linux_to_vk(event.evt_data.code);
            if (vk != 0) {
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = vk;
                if (event.evt_data.value == 0) {
                    input.ki.dwFlags = KEYEVENTF_KEYUP;
                }
                SendInput(1, &input, sizeof(INPUT));
            }
        }
    }
    return 0;
}

int dev_release_virt_keys(struct epoll_context* ep_ctx, struct device* device) {
    (void)ep_ctx;
    (void)device;
    return 0;
}

int dev_release_virt_keys_all(struct epoll_context* ep_ctx, device_arr* devices) {
    (void)ep_ctx;
    (void)devices;
    // Release modifiers on Windows if any were held
    INPUT inputs[6] = { 0 };
    WORD vks[] = { VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU };
    for (int i = 0; i < 6; i++) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wVk = vks[i];
        inputs[i].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    SendInput(6, inputs, sizeof(INPUT));
    return 0;
}

int dev_ctrl_devices(struct d_context* d_ctx, device_arr* devices, enum device_ctrl ctrl) {
    (void)devices;
    s_d_ctx = d_ctx;
    if (ctrl == DEV_CTRL_CAPTURE) {
        s_capturing = true;
        s_has_last_mouse = false;
        if (s_kb_hook == NULL) {
            s_kb_hook = SetWindowsHookExA(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
        }
        if (s_mouse_hook == NULL) {
            s_mouse_hook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);
        }
        printf("rcn: input capture enabled\n");
    } else {
        s_capturing = false;
        printf("rcn: input capture paused\n");
    }
    return 0;
}

int dev_close_dev(struct device_context* dev_ctx, struct epoll_stream* stream) {
    (void)dev_ctx;
    (void)stream;
    return 0;
}

int dev_handler(struct d_context* d_ctx, struct epoll_stream* stream, struct stream_item* stream_item) {
    (void)d_ctx;
    (void)stream;
    (void)stream_item;
    return 0;
}

#endif