#include "include/rcn.h"
#include "include/rcn_arg.h"
#include "include/rcn_relay.h"
#include <stdlib.h>
#include <string.h>

static const char* subactions[SUBACTION_COUNT] = {
    ARG_SUBACTION_PAUSE,
    ARG_SUBACTION_RESUME,
    ARG_SUBACTION_STOP,
    ARG_SUBACTION_LOG,
};
static const int subactions_length = sizeof(subactions) / sizeof(subactions[0]);

static const struct arg_help_data help_data[] = {
    {ARG_ACTION_START, ARG_HELP_ACTION_START},
    {ARG_ACTION_CONNECT, ARG_HELP_ACTION_CONNECT},
    {ARG_ACTION_SERVER, ARG_HELP_ACTION_SERVER},
    {ARG_ACTION_CLIENT, ARG_HELP_ACTION_CLIENT},
    {ARG_SUBACTION_PAUSE, ARG_HELP_SUBACTION_PAUSE},
    {ARG_SUBACTION_RESUME, ARG_HELP_SUBACTION_RESUME},
    {ARG_SUBACTION_STOP, ARG_HELP_SUBACTION_STOP},
};
static const int help_data_length = sizeof(help_data) / sizeof(help_data[0]);

static const struct arg_handler handlers[] = {
    {ARG_FLAG_PORT, ARG_FLAG_LONG_PORT, arg_port},
    {ARG_FLAG_HOST, ARG_FLAG_LONG_HOST, arg_host},
    {ARG_FLAG_DEVICES, ARG_FLAG_LONG_DEVICES, arg_devices},
    {ARG_FLAG_HELP, ARG_FLAG_LONG_HELP, arg_help},
    {ARG_SUBACTION_PAUSE, ARG_SUBACTION_PAUSE, arg_daemon},
    {ARG_SUBACTION_RESUME, ARG_SUBACTION_RESUME, arg_daemon},
    {ARG_SUBACTION_STOP, ARG_SUBACTION_STOP, arg_daemon},
    {ARG_SUBACTION_LOG, ARG_SUBACTION_LOG, arg_daemon},
};
static const int handlers_length = sizeof(handlers) / sizeof(handlers[0]);

static void print_help(void) {
    printf("Usage: rcn <action> <flag> <value> ...\n\n");
    printf("Possible actions are:");
    printf("\n\t%s: %s\n", ARG_ACTION_START, ARG_DESC_ACTION_START);
    printf("\n\t%s: %s\n", ARG_ACTION_CONNECT, ARG_DESC_ACTION_CONNECT);
    printf("\n\t%s: %s\n", ARG_ACTION_SERVER, ARG_DESC_ACTION_SERVER);
    printf("\n\t%s: %s\n", ARG_ACTION_CLIENT, ARG_DESC_ACTION_CLIENT);
    printf("\n");

    printf("Possible subaction are:");
    printf("\n\t%s: %s\n", ARG_SUBACTION_PAUSE, ARG_DESC_SUBACTION_PAUSE);
    printf("\n\t%s: %s\n", ARG_SUBACTION_RESUME, ARG_DESC_SUBACTION_RESUME);
    printf("\n\t%s: %s\n", ARG_SUBACTION_STOP, ARG_DESC_SUBACTION_STOP);
    printf("\n\t%s: %s\n", ARG_SUBACTION_LOG, ARG_DESC_SUBACTION_LOG);
    printf("\n");

    printf("Possible flags are:\n");
    printf("\t%s/%s: %s\n", ARG_FLAG_PORT, ARG_FLAG_LONG_PORT, ARG_DESC_FLAG_PORT);
    printf("\tAvailable for actions:\n");
    printf("\t\t%s, %s\n", ARG_ACTION_START, ARG_ACTION_CONNECT);

    printf("\n\t%s/%s: %s\n", ARG_FLAG_HOST, ARG_FLAG_LONG_HOST, ARG_DESC_FLAG_HOST);
    printf("\tAvailable for action:\n");
    printf("\t\t%s\n", ARG_ACTION_CONNECT);

    printf("\n\t%s/%s: %s\n", ARG_FLAG_DEVICES, ARG_FLAG_LONG_DEVICES, ARG_DESC_FLAG_DEVICES);
    printf("\tAvailable for action:\n");
    printf("\t\t%s\n", ARG_ACTION_CONNECT);

    printf("\n\t%s/%s: %s\n", ARG_FLAG_HELP, ARG_FLAG_LONG_HELP, ARG_DESC_FLAG_HELP);
    printf("\tPossible values are:\n");
    printf("\t\t<(sub)action>\n");
}

static int handle_arg(int argc, char** argv, int* i, struct arg_context* arg_ctx) {
    bool found_handler = false;
    char* arg = argv[*i];
    for (int fi = 0; fi < handlers_length; fi++) {
        if (strcmp(handlers[fi].flag, arg) == 0 || strcmp(handlers[fi].flag_long, arg) == 0) {
            found_handler = true;
            CHECK(handlers[fi].handler(argc, argv, i, arg_ctx) == -1);
            break;
        }
    }
    if (found_handler == false) {
        if (*i == 1)
            EARG_UNKNOWN("action", arg);
        if (*i > 1)
            EARG_UNKNOWN("flag", arg);
    }
    return 0;
err:
    ERR_LOG("handle_arg");
    return -1;
}

static int validate_arg_ctx(struct arg_context* arg_ctx) {
    struct arg* args = (struct arg*)arg_ctx;
    size_t ctx_count = sizeof(struct arg_context) / sizeof(struct arg);
    for (size_t i = 0; i < ctx_count; i++) {
        struct arg* a = &args[i];
        if (a->info.needed && !a->info.provided)
            EARG_MISSING();
    }
    return 0;
err:
    ERR_LOG("validate_arg_ctx");
    return -1;
}

int arg_port(int argc, char** argv, int* i, struct arg_context* ctx) {
    if (*i + 1 >= argc)
        EARG_MISSING_VALUE(ARG_FLAG_PORT);
    if (ctx->port.info.needed == false)
        EARG_WRONG_FLAG(ARG_FLAG_PORT);
    if (ctx->port.info.provided == true)
        EARG_AGAIN(ARG_FLAG_PORT);
    int port = atoi(argv[++(*i)]);
    if (port == 0)
        EARG_INVALID(argv[*i], ARG_FLAG_PORT);
    ctx->port.val.v_int = port;
    ctx->port.info.provided = true;
    return 0;
err:
    ERR_LOG("arg_port");
    return -1;
}

int arg_host(int argc, char** argv, int* i, struct arg_context* ctx) {
    if (*i + 1 >= argc)
        EARG_MISSING_VALUE(ARG_FLAG_HOST);
    if (ctx->server.info.needed == false)
        EARG_WRONG_FLAG(ARG_FLAG_HOST);
    if (ctx->server.info.provided == true)
        EARG_AGAIN(ARG_FLAG_HOST);
    ctx->server.val.v_char = argv[++(*i)];
    ctx->server.info.provided = true;
    return 0;
err:
    ERR_LOG("arg_host");
    return -1;
}

int arg_devices(int argc, char** argv, int* i, struct arg_context* ctx) {
    if (*i + 1 >= argc)
        EARG_MISSING_VALUE(ARG_FLAG_DEVICES);
    if (ctx->devices.info.needed == false && ctx->devices.info.allowed == false)
        EARG_WRONG_FLAG(ARG_FLAG_DEVICES);
    if (ctx->devices.info.provided == true)
        EARG_AGAIN(ARG_FLAG_DEVICES);
    char_arr* dev_arr = &ctx->devices.val.v_char_arr;
    CHECK(u_array_init(&dev_arr->r, sizeof(char*), RCN_STD_CAPACITY) == -1);
    (*i)++;
    for (; *i < argc; (*i)++) {
        char* arg = argv[*i];
        for (int fi = 0; fi < handlers_length; fi++) {
            if (strcmp(handlers[fi].flag, arg) == 0) {
                (*i)--;
                goto end;
            }
        }
        u_array_add(&dev_arr->r, &arg);
    }
end:
    ctx->devices.info.provided = true;
    return 0;
err:
    ERR_LOG("arg_devices");
    return -1;
}

int arg_daemon(int argc, char** argv, int* i, struct arg_context* ctx) {
    if (ctx->daemon.info.needed == false)
        EARG_WRONG_FLAG(ARG_FLAG_DEVICES);
    if (ctx->daemon.info.provided == true)
        EARG_AGAIN(ARG_FLAG_DEVICES);
    for (; *i < argc; (*i)++) {
        const char* arg = argv[*i];
        for (int j = 0; j < subactions_length; j++) {
            if (strcmp(arg, subactions[j]) == 0) {
                ctx->daemon.val.saction = j;
            }
        }
    }
    ctx->daemon.info.provided = true;
    return 0;
err:
    ERR_LOG("arg_daemon");
    return -1;
}

int arg_help(int argc, char** argv, int* i, struct arg_context* ctx) {
    if (ctx->help.info.needed == false)
        EARG_WRONG_FLAG(ARG_FLAG_HELP);
    if (ctx->help.info.provided == true)
        EARG_AGAIN(ARG_FLAG_HELP);
    if (argc == 2) {
        print_help();
        ctx->help.info.provided = true;
        return 0;
    }
    for ((*i)++; *i < argc; (*i)++) {
        char* action = argv[*i];
        bool found_action = false;
        for (int j = 0; j < help_data_length; j++) {
            if (strcmp(help_data[j].action, action) == 0) {
                printf("%s:\n\t%s\n", help_data[j].action, help_data[j].help);
                found_action = true;
                break;
            }
        }
        CHECK(found_action == false);
    }
    ctx->help.info.provided = true;
    return 0;
err:
    ERR_LOG("arg_help");
    return -1;
}

int parse_args(int argc, char** argv, int start_arg, struct arg_context* ctx) {
    for (int i = start_arg; i < argc; i++) {
        CHECK(handle_arg(argc, argv, &i, ctx) == -1);
    }
    CHECK(validate_arg_ctx(ctx) == -1);
    return 0;
err:
    ERR_LOG("parse_args");
    return -1;
}

int subaction_to_rcn_msg(enum subaction_type saction) {
    switch (saction) {
        case SUBACTION_PAUSE: return RELAY_HEADER_PAUSE;
        case SUBACTION_RESUME: return RELAY_HEADER_RESUME;
        case SUBACTION_STOP: return RELAY_HEADER_STOP;
        default:
            ERR_LOG("subaction_to_rcn_msg");
            return -1;
    }
}