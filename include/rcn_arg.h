#ifndef RCN_RCN_ARG_H
#define RCN_RCN_ARG_H

#include "rcn_platform.h"

#define ARG_ACTION_START 	"start"
#define ARG_ACTION_CONNECT 	"connect"
#define ARG_ACTION_SERVER   "server"
#define ARG_ACTION_CLIENT   "client"
#define ARG_ACTION_WORKER_SERVER "__worker_server"
#define ARG_ACTION_WORKER_CLIENT "__worker_client"

#define ARG_SUBACTION_PAUSE 	"pause"
#define ARG_SUBACTION_RESUME 	"resume"
#define ARG_SUBACTION_STOP 	    "stop"
#define ARG_SUBACTION_LOG       "log"

#define ARG_DESC_ACTION_START       "Start the server, on the given port, in the background"
#define ARG_DESC_ACTION_CONNECT     "Connect to the given server and capture the list of devices"
#define ARG_DESC_ACTION_SERVER      "Interact with the server daemon via subactions"
#define ARG_DESC_ACTION_CLIENT      "Interact with the client daemon via subactions"

#define ARG_DESC_SUBACTION_PAUSE   "Pause the capturing of the devices"
#define ARG_DESC_SUBACTION_RESUME  "Resume the capturing of the devices"
#define ARG_DESC_SUBACTION_STOP    "Stop the program"
#define ARG_DESC_SUBACTION_LOG     "Print the logs to stdout"

#define ARG_FLAG_PORT 	        "-p"
#define ARG_FLAG_LONG_PORT      "--port"
#define ARG_FLAG_HOST 	        "-s"
#define ARG_FLAG_LONG_HOST      "--server"
#define ARG_FLAG_DEVICES        "-d"
#define ARG_FLAG_LONG_DEVICES   "--devices"
#define ARG_FLAG_HELP           "-h"
#define ARG_FLAG_LONG_HELP      "--help"

#define ARG_DESC_FLAG_PORT       "Port on which to listen on/connect to"
#define ARG_DESC_FLAG_HOST       "Hostname/ip-address of the server"
#define ARG_DESC_FLAG_DEVICES    "List devices to capture, e.g. -d <evt1> <evt2> ... (on Windows: -d keyboard mouse)"
#define ARG_DESC_FLAG_HELP       "Print the help for rcn, also a list of actions and subactions can be supplied for more detailed infos"

#define ARG_HELP_ACTION_START   "Starts the server in the background, listening on the given port (-p/--port).\n\t" \
                                "Then, the client can run 'rcn connect ...' and transmit their captures devices.\n\t" \
                                "Every device specified in the 'rcn connect' command is recreated on the server and inputs are replayed."
#define ARG_HELP_ACTION_CONNECT "Connect to an already started rcn server on the given port (-p/--port).\n\t" \
                                "The server is specified with the '-s'/'--server' flag, which can either be an IPv4 address or the hostname.\n\t"\
                                "Every listed device/event-file is captured and copied to the server.\n\t"\
                                "When the client is actively running, input events are sent to the server."
#define ARG_HELP_ACTION_SERVER  "Interact with the server background process via subactions."
#define ARG_HELP_ACTION_CLIENT  "Interact with the client background process via subactions."

#define ARG_HELP_SUBACTION_PAUSE    "The client ungrabs the devices and no input data is sent to the server."
#define ARG_HELP_SUBACTION_RESUME   "The client regrabs the devices and input data is sent to the server."
#define ARG_HELP_SUBACTION_STOP     "If 'action' is set to 'client', the client disconnects and the server deletes the copied devices.\n\t" \
                                    "The server keeps running and new clients can connect.\n\t" \
                                    "If 'action' is set to 'server', the server closes the connection, deletes the copied devices and the background process is stopped.\n\t" \
                                    "No clients can connect anymore (unless started again)."

enum subaction_type {
    SUBACTION_PAUSE,
    SUBACTION_RESUME,
    SUBACTION_STOP,
    SUBACTION_LOG,
    SUBACTION_COUNT,
};

struct arg_info {
    bool needed;
    bool allowed;
    bool provided;
};

union arg_data {
    int v_int;
    enum subaction_type saction;
    char* v_char;
    char_arr v_char_arr;
};

struct arg {
    union arg_data val;
    struct arg_info info;
};

struct arg_context {
    struct arg port;
    struct arg server;
    struct arg devices;
    struct arg help;
    struct arg daemon;
};

struct arg_help_data {
    const char* action;
    const char* help;
};

#define EARG_COUNT(action, count_min, count_got) do {                                                     \
    fprintf(stderr, "err: not enough arguments for action '%s', expected (at least) %d but got %d\n",   \
        action, count_min, count_got);                                                                  \
    goto err;                                                                                           \
} while (0)

#define EARG_INVALID(value, type) do {                        \
    fprintf(stderr, "err: invalid value '%s' for '%s'\n",   \
        value, type);                                       \
    goto err;                                               \
} while (0)

#define EARG_UNKNOWN(type, value) do {            \
    fprintf(stderr, "err: unknown %s '%s'\n",   \
        type, value);                           \
    goto err;                                   \
} while (0)

#define EARG_MISSING_VALUE(flag) do {                             \
    fprintf(stderr, "err: missing value for flag '%s'\n", flag);\
    goto err;                                                   \
} while (0)

#define EARG_MISSING() do {                                       \
    fprintf(stderr, "err: missing flag/value\n");               \
    goto err;                                                   \
} while (0)

#define EARG_WRONG_FLAG(flag) do {                                            \
    fprintf(stderr, "err: invalid flag '%s'\n", flag);                      \
    goto err;                                                               \
} while (0)

#define EARG_AGAIN(flag) do {                                            \
    fprintf(stderr, "err: flag '%s' already used\n", flag);            \
    goto err;                                                          \
} while (0)

typedef int (*arg_handler_t)(int argc, char** argv, int* i, struct arg_context* ctx);

struct arg_handler {
    const char* flag;
    const char* flag_long;
    arg_handler_t handler;
};

int arg_port(int argc, char** argv, int* i, struct arg_context* ctx);
int arg_host(int argc, char** argv, int* i, struct arg_context* ctx);
int arg_devices(int argc, char** argv, int* i, struct arg_context* ctx);
int arg_daemon(int argc, char** argv, int* i, struct arg_context* ctx);
int arg_help(int argc, char** argv, int* i, struct arg_context* ctx);

int parse_args(int argc, char** argv, int start_arg, struct arg_context* ctx);

int subaction_to_rcn_msg(enum subaction_type saction);

#endif //RCN_RCN_ARG_H