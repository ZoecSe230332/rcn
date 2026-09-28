#ifndef RCN_RCN_UTIL_H
#define RCN_RCN_UTIL_H

#include "rcn_platform.h"
#include <stdio.h>

struct u_array {
    void* data;
    size_t size; 	    // size of each element
    size_t capacity; 	// total capacity
    size_t length; 	    // current amount of elements
};

struct u_queue {
    bool is_empty;
    struct u_array data_array;
};

#define U_DEFINE_ARR(name, type) \
typedef struct {                 \
    struct u_array r;            \
} (name);

#define U_DEFINE_QUEUE(name, type)  \
typedef struct {                    \
    struct u_queue r;               \
} (name);

/* rcn_util.c, u_misc */
void u_safe_free(void** ptr);
int u_close_connection(rcn_socket_t fd);

/* rcn_util.c, u_array */
int u_array_init(struct u_array* arr, size_t size, size_t capacity);
int u_array_resize(struct u_array* arr, size_t extra_elements);
int u_array_add(struct u_array* arr, void* data);
int u_array_set(struct u_array* arr, void* data, size_t i);
int u_array_delete(struct u_array* arr);
int u_array_remove(struct u_array* arr, size_t i);
int u_array_remove_get(struct u_array* arr, void* element, size_t i);
int u_array_getr(struct u_array* arr, void** element, size_t i);
int u_array_getv(struct u_array* arr, void* element, size_t i);
ssize_t u_array_find_index(struct u_array* arr, void* element);
int u_array_free(struct u_array* arr);

/* rcn_util.c, u_queue */
int u_queue_init(struct u_queue* queue, size_t size, size_t capacity);
int u_queue_push(struct u_queue* queue, void* element);
int u_queue_peek(struct u_queue* queue, void** element);
int u_queue_pop(struct u_queue* queue, void* element);
int u_queue_free(struct u_queue* queue);

#endif //RCN_RCN_UTIL_H
