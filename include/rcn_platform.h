#ifndef RCN_PLATFORM_H
#define RCN_PLATFORM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <afunix.h>
    #include <windows.h>
    #include <basetsd.h>
    #include <direct.h>
    #include <io.h>

    typedef SSIZE_T ssize_t;
    typedef SOCKET rcn_socket_t;
    #define RCN_INVALID_SOCKET INVALID_SOCKET
    #define RCN_SOCKET_ERROR SOCKET_ERROR
    #define rcn_close_socket(s) closesocket(s)
    #define rcn_send(s, buf, len) send((s), (const char*)(buf), (int)(len), 0)
    #define rcn_recv(s, buf, len) recv((s), (char*)(buf), (int)(len), 0)
    #define rcn_sleep_ms(ms) Sleep(ms)

    static inline int rcn_set_nonblocking(rcn_socket_t s) {
        u_long mode = 1;
        return ioctlsocket(s, FIONBIO, &mode);
    }

    static inline bool rcn_is_blocking_error(int err) {
        return (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == 0);
    }

    static inline int rcn_get_socket_error(void) {
        return WSAGetLastError();
    }

    static inline int rcn_platform_init(void) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
            return -1;
        }
        return 0;
    }

    static inline void rcn_platform_cleanup(void) {
        WSACleanup();
    }

    static inline const char* rcn_get_daemon_dir(void) {
        static char dir[MAX_PATH] = { 0 };
        if (dir[0] == '\0') {
            char temp[MAX_PATH];
            GetTempPathA(MAX_PATH, temp);
            snprintf(dir, sizeof(dir), "%srcn\\", temp);
        }
        return dir;
    }

    static inline const char* rcn_get_server_log_path(void) {
        static char path[MAX_PATH] = { 0 };
        if (path[0] == '\0') {
            snprintf(path, sizeof(path), "%sserver.log", rcn_get_daemon_dir());
        }
        return path;
    }

    static inline const char* rcn_get_client_log_path(void) {
        static char path[MAX_PATH] = { 0 };
        if (path[0] == '\0') {
            snprintf(path, sizeof(path), "%sclient.log", rcn_get_daemon_dir());
        }
        return path;
    }

    static inline const char* rcn_get_server_sock_path(void) {
        static char path[MAX_PATH] = { 0 };
        if (path[0] == '\0') {
            snprintf(path, sizeof(path), "%sserver.sock", rcn_get_daemon_dir());
        }
        return path;
    }

    static inline const char* rcn_get_client_sock_path(void) {
        static char path[MAX_PATH] = { 0 };
        if (path[0] == '\0') {
            snprintf(path, sizeof(path), "%sclient.sock", rcn_get_daemon_dir());
        }
        return path;
    }

    static inline int rcn_mkdir(const char* path) {
        return _mkdir(path);
    }

#else // Linux / POSIX
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <errno.h>

    typedef int rcn_socket_t;
    #define RCN_INVALID_SOCKET (-1)
    #define RCN_SOCKET_ERROR (-1)
    #define rcn_close_socket(s) close(s)
    #define rcn_send(s, buf, len) send((s), (buf), (len), 0)
    #define rcn_recv(s, buf, len) recv((s), (buf), (len), 0)
    #define rcn_sleep_ms(ms) usleep((ms) * 1000)

    static inline int rcn_set_nonblocking(rcn_socket_t s) {
        int flags = fcntl(s, F_GETFL, 0);
        if (flags == -1) return -1;
        return fcntl(s, F_SETFL, flags | O_NONBLOCK);
    }

    static inline bool rcn_is_blocking_error(int err) {
        return (err == EAGAIN || err == EWOULDBLOCK || err == 0);
    }

    static inline int rcn_get_socket_error(void) {
        return errno;
    }

    static inline int rcn_platform_init(void) {
        return 0;
    }

    static inline void rcn_platform_cleanup(void) {
    }

    #define RCN_POSIX_DAEMON_DIR_PATH "/tmp/rcn/"
    static inline const char* rcn_get_daemon_dir(void) {
        return RCN_POSIX_DAEMON_DIR_PATH;
    }

    static inline const char* rcn_get_server_log_path(void) {
        return RCN_POSIX_DAEMON_DIR_PATH "server.log";
    }

    static inline const char* rcn_get_client_log_path(void) {
        return RCN_POSIX_DAEMON_DIR_PATH "client.log";
    }

    static inline const char* rcn_get_server_sock_path(void) {
        return RCN_POSIX_DAEMON_DIR_PATH "server.sock";
    }

    static inline const char* rcn_get_client_sock_path(void) {
        return RCN_POSIX_DAEMON_DIR_PATH "client.sock";
    }

    static inline int rcn_mkdir(const char* path) {
        return mkdir(path, 0755);
    }

#endif

#endif // RCN_PLATFORM_H
