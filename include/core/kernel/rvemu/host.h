#ifndef RVEMU_HOST_H
#define RVEMU_HOST_H

#include <stdint.h>

typedef struct rv64_host {
    void *context;

    int (*write_output)(const void *data, uint64_t length, void *context);
    int (*write_error)(const void *data, uint64_t length, void *context);
    void (*report_error)(const char *message, uint64_t value_a, uint64_t value_b, void *context);
    int (*read_file_bytes)(const char *path, uint8_t **out_buffer, uint64_t *out_length, void *context);
    int exit_code;
    int exited;
} rv64_host_t;

void rvemu_host_init(rv64_host_t *host);

#endif