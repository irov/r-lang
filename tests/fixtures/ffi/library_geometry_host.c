/* B10 (R-FFI-0064, R-FFI-0065): a C program that uses the library of library_geometry.r through
 * the header r-front wrote for it. Without arguments it runs the API from the main thread and
 * four threads of its own; "early" calls an export before geometry_initialize and "late" after
 * geometry_shutdown, which ends the process with the contract_violation panic. Compiled with
 * R_GEOMETRY_DLOPEN it opens the library at run time instead of linking it. */
#include "geometry.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifdef R_GEOMETRY_DLOPEN
#include <dlfcn.h>

int main(int argc, char **argv) {
    void *library;
    int32_t (*initialize)(void);
    void (*shutdown)(void);
    point (*midpoint)(point, point);
    point left = {1.0, 1.0};
    point right = {3.0, 5.0};
    point middle;

    if (argc != 2) {
        return 64;
    }
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        (void)fprintf(stderr, "%s\n", dlerror());
        return 30;
    }
    *(void **)&initialize = dlsym(library, "geometry_initialize");
    *(void **)&shutdown = dlsym(library, "geometry_shutdown");
    *(void **)&midpoint = dlsym(library, "geometry_midpoint");
    if ((initialize == NULL) || (shutdown == NULL) || (midpoint == NULL) ||
        (dlsym(library, "r_runtime_hosted_start") != NULL)) {
        return 31;
    }
    if (initialize() != 0) {
        return 32;
    }
    middle = midpoint(left, right);
    if ((middle.x != 2.0) || (middle.y != 3.0)) {
        return 33;
    }
    shutdown();
    (void)printf("dlopen ok\n");
    return 0;
}
#else

static void *r_geometry_worker(void *argument) {
    (void)argument;
    for (int index = 0; index < 1000; ++index) {
        point left = {0.0, 0.0};
        point right = {2.0, 4.0};
        point middle = geometry_midpoint(left, right);
        if ((middle.x != 1.0) || (middle.y != 2.0)) {
            return (void *)1;
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    point left = {1.0, 2.0};
    point right = {3.0, 6.0};
    point middle;
    number value;
    flags bits = {1U, 99U};
    segment line = {{1.0, 1.0}, {4.0, 5.0}};
    node first;
    node second;
    block packed = {{7U, 1000U}, {1U, 2U, 30U}, 200U, shape_square};
    void *counter;
    pthread_t threads[4];
    uint64_t calls;

    if ((argc == 2) && (strcmp(argv[1], "early") == 0)) {
        middle = geometry_midpoint(left, right);
        return 1;
    }
    if (geometry_initialize() != 0) {
        return 10;
    }
    if (geometry_initialize() != 1) {
        return 11;
    }
    if ((argc == 2) && (strcmp(argv[1], "late") == 0)) {
        geometry_shutdown();
        middle = geometry_midpoint(left, right);
        return 1;
    }
    middle = geometry_midpoint(left, right);
    if ((middle.x != 2.0) || (middle.y != 4.0)) {
        return 12;
    }
    geometry_scale(&middle, 2.0);
    if ((middle.x != 4.0) || (middle.y != 8.0)) {
        return 13;
    }
    if ((geometry_area(shape_square, 3.0) != 9.0) || (geometry_area(shape_circle, 1.0) != 3.0)) {
        return 14;
    }
    value.real = 2.5;
    if (geometry_number_real(value) != 2.5) {
        return 15;
    }
    if (geometry_flags_layer(bits) != 99U) {
        return 16;
    }
    /* B10-1: a struct of two structs of two doubles passes in four floating registers. */
    if (geometry_segment_length_squared(line) != 25.0) {
        return 17;
    }
    first.value = 3;
    first.next = &second;
    second.value = 4;
    second.next = &first;
    if (geometry_node_sum(&first, 5) != 17) {
        return 18;
    }
    if (geometry_block_length(packed) != 1230U) {
        return 19;
    }
    packed.kind = shape_circle;
    if (geometry_block_length(packed) != 0U) {
        return 20;
    }
    counter = geometry_counter_new();
    if ((counter == NULL) || (geometry_counter_add(counter, 5U) != 5U) ||
        (geometry_counter_add(counter, 7U) != 12U)) {
        return 21;
    }
    geometry_counter_free(counter);
    for (int index = 0; index < 4; ++index) {
        if (pthread_create(&threads[index], NULL, r_geometry_worker, NULL) != 0) {
            return 22;
        }
    }
    for (int index = 0; index < 4; ++index) {
        void *result = NULL;
        if ((pthread_join(threads[index], &result) != 0) || (result != NULL)) {
            return 23;
        }
    }
    calls = geometry_calls();
    if (calls != 4007U) {
        (void)fprintf(stderr, "calls %llu\n", (unsigned long long)calls);
        return 24;
    }
    geometry_shutdown();
    geometry_shutdown();
    /* The static objects are not restored: a shut down library stays shut down. */
    if (geometry_initialize() != 1) {
        return 25;
    }
    (void)printf("ok %s\n", GEOMETRY_VERSION);
    return 0;
}
#endif
