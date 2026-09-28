#ifndef RCN_H
#define RCN_H

#include "rcn_platform.h"
#include "rcn_types.h"
#include <errno.h>
#include <stdio.h>

#define ERR_GOTO(label, ...) 					\
    do { 							            \
	fprintf(stderr, __VA_ARGS__); 				\
	goto label; 						        \
    } while (0)

#define DO_GOTO(expr, label)                    \
    do {                                        \
    (expr);                                     \
    goto label;                                 \
    } while (0)

#define ERRNO_GOTO(label, msg) 					        \
    do { 							                    \
	fprintf(stderr, "%s: %s\n", msg, strerror(errno)); 	\
	goto label; 						                \
    } while (0)

#define ERR_LOG(...)                                    \
    do {                                                \
        fprintf(stderr, "err: ");                       \
        fprintf(stderr, __VA_ARGS__);       			\
        fprintf(stderr, ": %s\n", strerror(errno));     \
    } while (0)

#define CHECK(condition) 		    \
    do { 					        \
	if ((condition)) goto err; 		\
    } while(0)

#define RCN_DAEMON_DIR_PATH rcn_get_daemon_dir()
#define RCN_SERVER_LOG_PATH rcn_get_server_log_path()
#define RCN_CLIENT_LOG_PATH rcn_get_client_log_path()
#define RCN_SERVER_SOCKET_PATH rcn_get_server_sock_path()
#define RCN_SERVER_SOCKET_LEN (strlen(RCN_SERVER_SOCKET_PATH) + 1)
#define RCN_CLIENT_SOCKET_PATH rcn_get_client_sock_path()
#define RCN_CLIENT_SOCKET_LEN (strlen(RCN_CLIENT_SOCKET_PATH) + 1)
#define RCN_STD_CAPACITY 10
#define RCN_DEV_MAX_NAME_LEN 255

#define RCN_PROC_NAME_SERVER "rcn_server"
#define RCN_PROC_NAME_CLIENT "rcn_client"
#define RCN_PROC_NAME_RELAY  "rcn_relay"

#endif // !RCN_H
