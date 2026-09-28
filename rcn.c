#include "include/rcn.h"
#include "include/rcn_arg.h"
#include "include/rcn_daemon.h"
#include "include/rcn_device.h"
#include <string.h>
#include <signal.h>
#include <stdio.h>

#ifdef _WIN32
static BOOL WINAPI console_ctrl_handler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            fprintf(stderr, "\n[rcn] Caught termination signal. Releasing all inputs and stopping...\n");
            fflush(stderr);
            dev_cleanup_all();
            rcn_platform_cleanup();
            ExitProcess(0);
        default:
            return FALSE;
    }
}
#else
static void sigint_handler(int sig) {
    (void)sig;
    fprintf(stderr, "\n[rcn] Caught termination signal. Releasing all devices and stopping...\n");
    fflush(stderr);
    dev_cleanup_all();
    exit(0);
}
#endif

static int subaction_daemon(enum daemon_type d_type, enum subaction_type sa_type) {
    struct relay_arg r_arg = { 0 };
    r_arg.d_type = d_type;
    int msg = subaction_to_rcn_msg(sa_type);
    CHECK(msg == -1);
    r_arg.header_sent = (enum relay_msg_header)msg;
    r_arg.sleep = false;
    CHECK(relay_start(r_arg) == -1);
    return 0;
err:
    ERR_LOG("subaction_daemon");
    return -1;
}

/* rcn start -p 8000 */
int action_start(int argc, char** argv) {
    if (argc < 4)
        EARG_COUNT(ARG_ACTION_START, 4, argc);
    struct arg_context arg_ctx = { 0 };
    arg_ctx.port.info.needed = true;
    CHECK(parse_args(argc, argv, 2, &arg_ctx) == -1);
    struct daemon_arg d_arg = { 0 };
    d_arg.type = DAEMON_SERVER;
    d_arg.port = arg_ctx.port.val.v_int;

    printf("\n======================================================\n");
    printf("              rcn - Server Mode\n");
    printf("  Port: %d\n", d_arg.port);
    printf("  Emergency Stop: [Ctrl + Alt + Esc]\n");
    printf("  Press Ctrl + C in this terminal to stop\n");
    printf("======================================================\n\n");
    u_print_server_info(d_arg.port);
    fflush(stdout);

    CHECK(d_init(d_arg) == -1);
    return 0;
err:
    ERR_LOG("action_start");
    return -1;
}

/* rcn connect -s <...> -p 800 -d <...> */
int action_connect(int argc, char** argv) {
#ifndef _WIN32
    if (argc < 8)
        EARG_COUNT(ARG_ACTION_CONNECT, 8, argc);
#else
    if (argc < 6)
        EARG_COUNT(ARG_ACTION_CONNECT, 6, argc);
#endif
    struct arg_context arg_ctx = { 0 };
#ifndef _WIN32
    arg_ctx.devices.info.needed = true;
#else
    arg_ctx.devices.info.needed = false;
    arg_ctx.devices.info.allowed = true;
#endif
    arg_ctx.port.info.needed = true;
    arg_ctx.server.info.needed = true;
    CHECK(parse_args(argc, argv, 2, &arg_ctx) == -1);
    struct daemon_arg d_arg = { 0 };
    d_arg.port = arg_ctx.port.val.v_int;
    d_arg.host = arg_ctx.server.val.v_char;
    if (arg_ctx.devices.info.provided)
        d_arg.devices_arg = &arg_ctx.devices.val.v_char_arr;
    else
        d_arg.devices_arg = NULL;
    d_arg.type = DAEMON_CLIENT;

    printf("\n======================================================\n");
    printf("              rcn - Client Mode\n");
    printf("  Target Server: %s:%d\n", d_arg.host, d_arg.port);
    printf("  Emergency Stop: [Ctrl + Alt + Esc]\n");
    printf("  Press Ctrl + C in this terminal to disconnect and stop\n");
    printf("======================================================\n\n");
    fflush(stdout);

    int res = d_init(d_arg);
    if (d_arg.devices_arg != NULL)
        u_array_free(&d_arg.devices_arg->r);
    CHECK(res == -1);
    return 0;
err:
    ERR_LOG("action_connect");
    return -1;
}

int action_server(int argc, char** argv) {
    if (argc == 2) {
        char* default_argv[] = { argv[0], "start", "-p", "9999" };
        return action_start(4, default_argv);
    }
    if (argc >= 4 && (strcmp(argv[2], "-p") == 0 || strcmp(argv[2], "--port") == 0)) {
        return action_start(argc, argv);
    }
    if (argc < 3)
        EARG_COUNT(ARG_ACTION_SERVER, 3, argc);
    struct arg_context arg_ctx = { 0 };
    arg_ctx.daemon.info.needed = true;
    CHECK(parse_args(argc, argv, 2, &arg_ctx) == -1);
    if (arg_ctx.daemon.val.saction == SUBACTION_LOG)
        CHECK(d_print_log(DAEMON_SERVER) == -1);
    else
        CHECK(subaction_daemon(DAEMON_SERVER, arg_ctx.daemon.val.saction) == -1);
    return 0;
err:
    ERR_LOG("action_server");
    return -1;
}

int action_client(int argc, char** argv) {
    if (argc < 3)
        EARG_COUNT(ARG_ACTION_CLIENT, 3, argc);
    struct arg_context arg_ctx = { 0 };
    arg_ctx.daemon.info.needed = true;
    CHECK(parse_args(argc, argv, 2, &arg_ctx) == -1);
    if (arg_ctx.daemon.val.saction == SUBACTION_LOG)
        CHECK(d_print_log(DAEMON_CLIENT) == -1);
    else
        CHECK(subaction_daemon(DAEMON_CLIENT, arg_ctx.daemon.val.saction) == -1);
    return 0;
err:
    ERR_LOG("action_client");
    return -1;
}

int action_worker_server(int argc, char** argv) {
    struct arg_context arg_ctx = { 0 };
    arg_ctx.port.info.needed = true;
    CHECK(parse_args(argc, argv, 2, &arg_ctx) == -1);
    struct daemon_arg d_arg = { 0 };
    d_arg.type = DAEMON_SERVER;
    d_arg.port = arg_ctx.port.val.v_int;
    u_print_server_info(d_arg.port);
    CHECK(d_init(d_arg) == -1);
    return 0;
err:
    ERR_LOG("action_worker_server");
    return -1;
}

int action_worker_client(int argc, char** argv) {
    struct arg_context arg_ctx = { 0 };
    arg_ctx.port.info.needed = true;
    arg_ctx.server.info.needed = true;
    arg_ctx.devices.info.needed = false;
    arg_ctx.devices.info.allowed = true;
    CHECK(parse_args(argc, argv, 2, &arg_ctx) == -1);
    struct daemon_arg d_arg = { 0 };
    d_arg.type = DAEMON_CLIENT;
    d_arg.port = arg_ctx.port.val.v_int;
    d_arg.host = arg_ctx.server.val.v_char;
    if (arg_ctx.devices.info.provided)
        d_arg.devices_arg = &arg_ctx.devices.val.v_char_arr;
    else
        d_arg.devices_arg = NULL;
    CHECK(d_init(d_arg) == -1);
    return 0;
err:
    ERR_LOG("action_worker_client");
    return -1;
}

int help(int argc, char** argv) {
    struct arg_context arg_ctx = { 0 };
    arg_ctx.help.info.needed = true;
    CHECK(parse_args(argc, argv, 1, &arg_ctx) == -1);
    return 0;
err:
    ERR_LOG("help");
    return -1;
}

int main(int argc, char** argv) {
    if (rcn_platform_init() == -1) {
        return -1;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
#endif

    if (argc < 2) {
        help(2, (char*[]){"", ARG_FLAG_HELP});
        ERR_GOTO(err, "\nerr: no action supplied\n");
    }

    const char* action = argv[1];

    if (strcmp(action, ARG_ACTION_START) == 0)
        CHECK(action_start(argc, argv) == -1);
    else if (strcmp(action, ARG_ACTION_CONNECT) == 0)
        CHECK(action_connect(argc, argv) == -1);
    else if (strcmp(action, ARG_ACTION_SERVER) == 0)
        CHECK(action_server(argc, argv) == -1);
    else if (strcmp(action, ARG_ACTION_CLIENT) == 0)
        CHECK(action_client(argc, argv) == -1);
    else if (strcmp(action, ARG_ACTION_WORKER_SERVER) == 0)
        CHECK(action_worker_server(argc, argv) == -1);
    else if (strcmp(action, ARG_ACTION_WORKER_CLIENT) == 0)
        CHECK(action_worker_client(argc, argv) == -1);
    else
        CHECK(help(argc, argv) == -1);

    rcn_platform_cleanup();
    return 0;
err:
    fprintf(stderr, "see 'rcn -h' for help\n");
    rcn_platform_cleanup();
    return -1;
}
