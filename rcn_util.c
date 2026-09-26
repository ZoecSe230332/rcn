#include "include/rcn.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* u_misc */
void u_safe_free(void** ptr) {
    free(*ptr);
    *ptr = NULL;
}

int u_close_connection(int fd) {
    int flags = TRY(fcntl(fd, F_GETFL, 0), -1);
    CHECK(fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1);
    // wait max. 1 second for the server to send a FIN paket, if not, force close the connection
    struct timeval tv = {};
    tv.tv_sec = 1;
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1);
    CHECK(shutdown(fd, SHUT_WR) == -1);
    uint8_t buffer[64] = { 0 };
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {}
    if (bytes_read == -1 && (errno != EAGAIN && errno != EWOULDBLOCK))
        goto err;
    close(fd);
    return 0;
err:
    close(fd);
    ERR_LOG("c_close_connection");
    return -1;
}

/* u_array */
static int resize(void** data, size_t elm_size, size_t* capacity,size_t extra_elements) {
    if (data == NULL)
        DO_GOTO(errno = EFAULT, err);
    size_t new_size = (*capacity + extra_elements) * elm_size;
    void* tmp = realloc(*data, new_size);
    CHECK(tmp == NULL);
    *data = tmp;
    *capacity += extra_elements;
    printf("resize\n");
    return 0;
err:
    ERR_LOG("u_array_resize");
    return -1;
}

struct u_array u_array_create(size_t size, size_t capacity) {
    struct u_array arr = {};
    arr.length = 0;
    arr.capacity = capacity;
    arr.size = size;
    arr.data = TRY(calloc(capacity, size), NULL);
    return arr;
err:
    ERR_LOG("u_array_create");
    return (struct u_array){.data = NULL};
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
    void* offset = arr->data + (arr->size * arr->length);
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
    void* offset = arr->data + (arr->size * i);
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
    CHECK(arr == NULL);
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
    void* dest = arr->data + (arr->size * i);
    void* src = arr->data + (arr->size * (i+1));
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
    void* dest = arr->data + (arr->size * i);
    memcpy(element, dest, arr->size);
    void* src = arr->data + (arr->size * (i+1));
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
    *element = arr->data + (arr->size * i);
    return 0;
err:
    ERR_LOG("u_array_getr");
    return -1;
}

int u_array_getv(struct u_array* arr, void* element, size_t i) {
    if (arr->data == NULL)
        DO_GOTO(errno = EFAULT, err);
    CHECK(i >= arr->length);
    void* offset = arr->data + (arr->size * i);
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
        void* offset = arr->data + (arr->size * i);
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
    CHECK(queue== NULL);
    u_safe_free(&queue->data_array.data);
    return 0;
err:
    ERR_LOG("u_queue_free");
    return -1;
}