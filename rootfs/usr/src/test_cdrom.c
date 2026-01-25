#include "userspace_init.h"
#include <stddef.h>

// Эти константы должны соответствовать vfs.h
#define O_RDONLY 0x01
#define O_WRONLY 0x02
#define O_RDWR   (O_RDONLY | O_WRONLY)

void main() {
    printf("--- Running CD-ROM Test ---\n");

    int fd = open("/dev/cdrom0", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open /dev/cdrom0. Error: %d\n", -fd);
        return;
    }

    printf("Successfully opened /dev/cdrom0 with fd: %d\n", fd);

    char buffer[512];
    // Попытаемся прочитать первые 512 байт
    int bytes_read = read(fd, buffer, 512);

    if (bytes_read < 0) {
        printf("Failed to read from device. Error: %d\n", -bytes_read);
    } else {
        printf("Read %d bytes successfully.\n", bytes_read);
        // Выведем несколько байт для проверки
        printf("First 16 bytes (hex): ");
        for (int i = 0; i < 16 && i < bytes_read; i++) {
            // Эта функция print_hex() должна быть в вашем userspace_init.h
            // если ее нет, мы можем просто печатать как числа
             printf_hex(buffer[i]);
             printf(" ");
        }
        printf("\n");
    }

    // Попытка записи (должна провалиться)
    int bytes_written = write(fd, "test", 4);
    if (bytes_written < 0) {
        // Ожидаемая ошибка EROFS (Read-only file system) - это успех
        printf("Write failed as expected. Error: %d\n", -bytes_written);
    } else {
        printf("Write succeeded unexpectedly! Bytes written: %d\n", bytes_written);
    }

    close(fd);
    printf("--- CD-ROM Test Finished ---\n");
}
