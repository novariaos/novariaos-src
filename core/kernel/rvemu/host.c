#include <log.h>

#include <core/fs/vfs.h>
#include <core/kernel/mem/allocator.h>
#include <core/kernel/rvemu/host.h>
#include <core/kernel/tty.h>

#define HOST_TTY_CHUNK 128

static int host_write_to_tty(const void *data, uint64_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t offset = 0;
    while (offset < length) {
        char chunk[HOST_TTY_CHUNK];
        int count = 0;
        while (offset < length && count < HOST_TTY_CHUNK - 1) {
            chunk[count] = (char)bytes[offset];
            count = count + 1;
            offset = offset + 1;
        }
        chunk[count] = '\0';
        tty_puts(chunk);
    }
    return 0;
}

static int host_write_output_impl(const void *data, uint64_t length, void *context) {
    (void)context;
    return host_write_to_tty(data, length);
}

static int host_write_error_impl(const void *data, uint64_t length, void *context) {
    (void)context;
    return host_write_to_tty(data, length);
}

static void host_report_error_impl(const char *message, uint64_t value_a, uint64_t value_b, void *context) {
    (void)context;
    LOG_WARN("rvemu: %s (0x%08x, 0x%08x)", message, (uint32_t)value_a, (uint32_t)value_b);
}

static int host_read_file_impl(const char *path, uint8_t **out_buffer, uint64_t *out_length, void *context) {
    (void)context;
    *out_buffer = NULL;
    *out_length = 0;

    size_t file_size = 0;
    const char *data = vfs_read(path, &file_size);
    if (data == NULL || file_size == 0) {
        return 1;
    }

    uint8_t *buffer = (uint8_t *)kmalloc(file_size);
    if (buffer == NULL) {
        return 1;
    }
    for (size_t index = 0; index < file_size; index++) {
        buffer[index] = (uint8_t)data[index];
    }

    *out_buffer = buffer;
    *out_length = (uint64_t)file_size;
    return 0;
}

void rvemu_host_init(rv64_host_t *host) {
    host->context = NULL;
    host->write_output = host_write_output_impl;
    host->write_error = host_write_error_impl;
    host->report_error = host_report_error_impl;
    host->read_file_bytes = host_read_file_impl;
    host->exit_code = 0;
    host->exited = 0;
}