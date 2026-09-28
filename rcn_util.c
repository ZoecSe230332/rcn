#include "include/rcn.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

/* u_misc */
void u_safe_free(void** ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

int u_close_connection(rcn_socket_t fd) {
    if (fd == RCN_INVALID_SOCKET)
        return 0;

#ifndef _WIN32
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval tv = { 0 };
    tv.tv_sec = 1;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    shutdown(fd, SHUT_WR);
    uint8_t buffer[64] = { 0 };
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {}
    close(fd);
#else
    u_long mode = 0;
    ioctlsocket(fd, FIONBIO, &mode);
    DWORD tv = 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    shutdown(fd, SD_BOTH);
    uint8_t buffer[64] = { 0 };
    while (recv(fd, (char*)buffer, sizeof(buffer), 0) > 0) {}
    closesocket(fd);
#endif
    return 0;
}

void u_print_server_info(int port) {
    printf("rcn: Server listening on port %d\n", port);
    printf("rcn: IP address(es) for client to connect to:\n");
    int count = 0;

#ifndef _WIN32
    struct ifaddrs *ifaddr = NULL, *ifa = NULL;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
                char ip_str[INET_ADDRSTRLEN] = { 0 };
                inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
                if (strncmp(ip_str, "127.", 4) != 0) {
                    printf("       -> %s (port %d, %s)\n", ip_str, port, ifa->ifa_name);
                    count++;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
#else
    char hostname[256] = { 0 };
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints = { 0 };
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = NULL;
        if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
            for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
                struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
                char ip_str[INET_ADDRSTRLEN] = { 0 };
                inet_ntop(AF_INET, &ipv4->sin_addr, ip_str, sizeof(ip_str));
                if (strncmp(ip_str, "127.", 4) != 0 && strncmp(ip_str, "169.254.", 8) != 0) {
                    printf("       -> %s (port %d)\n", ip_str, port);
                    count++;
                }
            }
            freeaddrinfo(res);
        }
    }
#endif
    if (count == 0) {
        printf("       -> 127.0.0.1 (port %d, localhost)\n", port);
    }
    fflush(stdout);
}

/* u_array */
static int resize(void** data, size_t elm_size, size_t* capacity, size_t extra_elements) {
    if (data == NULL)
        DO_GOTO(errno = EFAULT, err);
    size_t new_size = (*capacity + extra_elements) * elm_size;
    void* tmp = realloc(*data, new_size);
    CHECK(tmp == NULL);
    *data = tmp;
    *capacity += extra_elements;
    return 0;
err:
    ERR_LOG("u_array_resize");
    return -1;
}

struct u_array u_array_create(size_t size, size_t capacity) {
    struct u_array arr = { 0 };
    arr.length = 0;
    arr.capacity = capacity;
    arr.size = size;
    arr.data = calloc(capacity, size);
    if (arr.data == NULL) {
        ERR_LOG("u_array_create");
    }
    return arr;
}

int u_array_init(struct u_array* arr, size_t size, size_t capacity) {
    CHECK(size <= 0);
    CHECK(capacity <= 0);
    arr->length = 0;
    arr->capacity = capacity;
    arr->size = size;
    arr->data = calloc(capacity, size);
    CHECK(arr->data == NULL);
    return 0;
err:
    ERR_LOG("u_array_init");
    return -1;
}

int u_array_resize(struct u_array* arr, size_t extra_elements) {
    CHECK(resize(&arr->data, arr->size, &arr->capacity, extra_elements) == -1);
    return 0;
err:
    ERR_LOG("u_array_resize");
    return -1;
}

int u_array_add(struct u_array* arr, void* data) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    if (arr->length == arr->capacity)
        CHECK(u_array_resize(arr, arr->capacity) == -1);
    char* offset = (char*)arr->data + (arr->size * arr->length);
    memcpy(offset, data, arr->size);
    arr->length++;
    return 0;
err:
    ERR_LOG("u_array_add");
    return -1;
}

int u_array_set(struct u_array* arr, void* data, size_t i) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    while (i >= arr->capacity)
        CHECK(u_array_resize(arr, arr->capacity) == -1);
    char* offset = (char*)arr->data + (arr->size * i);
    memcpy(offset, data, arr->size);
    if (i >= arr->length)
        arr->length = i + 1;
    return 0;
err:
    ERR_LOG("u_array_set");
    return -1;
}

int u_array_delete(struct u_array* arr) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    CHECK(arr->length == 0);
    arr->length--;
    return 0;
err:
    ERR_LOG("u_array_delete");
    return -1;
}

int u_array_remove(struct u_array* arr, size_t i) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    CHECK(arr->length == 0);
    CHECK(i >= arr->length);
    char* dest = (char*)arr->data + (arr->size * i);
    char* src = (char*)arr->data + (arr->size * (i + 1));
    size_t n = (arr->length - i - 1) * arr->size;
    memmove(dest, src, n);
    arr->length--;
    return 0;
err:
    ERR_LOG("u_array_remove");
    return -1;
}

int u_array_remove_get(struct u_array* arr, void* element, size_t i) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    CHECK(arr->length == 0);
    CHECK(i >= arr->length);
    char* dest = (char*)arr->data + (arr->size * i);
    memcpy(element, dest, arr->size);
    char* src = (char*)arr->data + (arr->size * (i + 1));
    size_t n = (arr->length - i - 1) * arr->size;
    memmove(dest, src, n);
    arr->length--;
    return 0;
err:
    ERR_LOG("u_array_remove_get");
    return -1;
}

int u_array_getr(struct u_array* arr, void** element, size_t i) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    CHECK(i >= arr->length);
    *element = (char*)arr->data + (arr->size * i);
    return 0;
err:
    ERR_LOG("u_array_getr");
    return -1;
}

int u_array_getv(struct u_array* arr, void* element, size_t i) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    CHECK(i >= arr->length);
    char* offset = (char*)arr->data + (arr->size * i);
    memcpy(element, offset, arr->size);
    return 0;
err:
    ERR_LOG("u_array_getv");
    return -1;
}

ssize_t u_array_find_index(struct u_array* arr, void* element) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    for (size_t i = 0; i < arr->length; i++) {
        char* offset = (char*)arr->data + (arr->size * i);
        if (memcmp(offset, element, arr->size) == 0)
            return (ssize_t)i;
    }
    ERR_LOG("u_array_find_index");
    return -1;
err:
    ERR_LOG("u_array_find_index");
    return -1;
}

int u_array_free(struct u_array* arr) {
    CHECK(arr == NULL);
    u_safe_free(&arr->data);
    return 0;
err:
    ERR_LOG("u_array_free");
    return -1;
}

/* u_queue */
int u_queue_init(struct u_queue* queue, size_t size, size_t capacity) {
    CHECK(u_array_init(&queue->data_array, size, capacity) == -1);
    queue->is_empty = true;
    return 0;
err:
    ERR_LOG("u_queue_init");
    return -1;
}

int u_queue_push(struct u_queue* queue, void* element) {
    CHECK(u_array_add(&queue->data_array, element) == -1);
    queue->is_empty = false;
    return 0;
err:
    ERR_LOG("u_queue_push");
    return -1;
}

int u_queue_peek(struct u_queue* queue, void** element) {
    CHECK(queue->is_empty);
    CHECK(u_array_getr(&queue->data_array, element, 0) == -1);
    return 0;
err:
    ERR_LOG("u_queue_peek");
    return -1;
}

int u_queue_pop(struct u_queue* queue, void* element) {
    CHECK(queue->is_empty);
    CHECK(u_array_remove_get(&queue->data_array, element, 0) == -1);
    queue->is_empty = queue->data_array.length == 0;
    return 0;
err:
    ERR_LOG("u_queue_pop");
    return -1;
}

int u_queue_free(struct u_queue* queue) {
    CHECK(queue == NULL);
    u_safe_free(&queue->data_array.data);
    return 0;
err:
    ERR_LOG("u_queue_free");
    return -1;
}