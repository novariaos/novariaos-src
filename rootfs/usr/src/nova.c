// // SPDX-License-Identifier: LGPL-3.0-or-later

#include <core/kernel/log.h>

// #include <core/kernel/kstd.h>
// #include <core/drivers/keyboard.h>
// #include <core/kernel/vge/fb.h>
// #include <core/fs/vfs.h>

// #define MAX_LINES 20
// #define MAX_LINE_LENGTH 79
// #define EDITOR_HEIGHT 22

// // Editor buffer
// static char buffer[MAX_LINES][MAX_LINE_LENGTH + 1];
// static int cursor_x = 0;
// static int cursor_y = 0;
// static int num_lines = 1;
// static char current_filename[MAX_FILENAME];
// static bool modified = false;

// // Helper function to get string length
// static int strlen(const char* str) {
//     int len = 0;
//     while (str[len] != '\0') {
//         len++;
//     }
//     return len;
// }

// // Helper function to copy string
// static void strcpy(char* dest, const char* src) {
//     while (*src) {
//         *dest++ = *src++;
//     }
//     *dest = '\0';
// }

// // Clear the buffer
// static void clear_buffer(void) {
//     for (int i = 0; i < MAX_LINES; i++) {
//         for (int j = 0; j <= MAX_LINE_LENGTH; j++) {
//             buffer[i][j] = '\0';
//         }
//     }
//     cursor_x = 0;
//     cursor_y = 0;
//     num_lines = 1;
//     modified = false;
// }

// // Load file into buffer
// static void load_file(const char* filename) {
//     size_t size;
//     const char* data = vfs_read(filename, &size);
    
//     if (data == NULL) {
//         // New file
//         clear_buffer();
//         return;
//     }
    
//     clear_buffer();
    
//     int line = 0;
//     int col = 0;
    
//     for (size_t i = 0; i < size && line < MAX_LINES; i++) {
//         if (data[i] == '\n') {
//             buffer[line][col] = '\0';
//             line++;
//             col = 0;
//             if (line < MAX_LINES) {
//                 num_lines = line + 1;
//             }
//         } else if (col < MAX_LINE_LENGTH) {
//             buffer[line][col++] = data[i];
//         }
//     }
    
//     if (col > 0) {
//         buffer[line][col] = '\0';
//     }
// }

// // Save buffer to file
// static int save_file(void) {
//     char file_data[MAX_LINES * (MAX_LINE_LENGTH + 1)];
//     int pos = 0;
    
//     for (int i = 0; i < num_lines && pos < (MAX_LINES * (MAX_LINE_LENGTH + 1)) - 1; i++) {
//         int j = 0;
//         while (buffer[i][j] != '\0' && pos < (MAX_LINES * (MAX_LINE_LENGTH + 1)) - 1) {
//             file_data[pos++] = buffer[i][j++];
//         }
//         if (i < num_lines - 1) {
//             file_data[pos++] = '\n';
//         }
//     }
//     file_data[pos] = '\0';
    
//     int result = vfs_create(current_filename, file_data, pos);
//     if (result >= 0) {
//         modified = false;
//         return 0;
//     }
//     return -1;
// }

// // Draw the editor screen
// static void draw_screen(void) {
//     clear_screen();
    
//     // Draw title bar
//     kprint("  Nova Text Editor - ", 11);
//     kprint(current_filename, 15);
//     if (modified) {
//         kprint(" [Modified]", 12);
//     }
//     kprint("\n", 7);
    
//     // Draw separator
//     for (int i = 0; i < 80; i++) {
//         kprint("-", 8);
//     }
//     kprint("\n", 7);
    
//     // Draw buffer content
//     for (int i = 0; i < MAX_LINES; i++) {
//         if (i < num_lines) {
//             kprint(buffer[i], 15);
//         }
//         kprint("\n", 7);
//     }
    
//     // Draw status bar
//     kprint("\n", 7);
//     for (int i = 0; i < 80; i++) {
//         kprint("-", 8);
//     }
//     kprint("\n", 7);
//     kprint("^S Save  ^X Exit  Arrow Keys Move  Type to edit", 11);
    
//     // Set cursor position (offset by 2 lines for title bar and separator)
//     terminal_set_cursor(cursor_x, cursor_y + 2);
// }

// // Insert character at cursor position
// static void insert_char(char c) {
//     if (cursor_y >= MAX_LINES) return;
    
//     int line_len = strlen(buffer[cursor_y]);
    
//     if (line_len >= MAX_LINE_LENGTH) return;
    
//     // Shift characters to the right
//     for (int i = line_len; i >= cursor_x; i--) {
//         buffer[cursor_y][i + 1] = buffer[cursor_y][i];
//     }
    
//     buffer[cursor_y][cursor_x] = c;
//     cursor_x++;
//     modified = true;
// }

// // Delete character at cursor position (backspace)
// static void delete_char(void) {
//     if (cursor_x > 0) {
//         int line_len = strlen(buffer[cursor_y]);
        
//         // Shift characters to the left
//         for (int i = cursor_x - 1; i < line_len; i++) {
//             buffer[cursor_y][i] = buffer[cursor_y][i + 1];
//         }
        
//         cursor_x--;
//         modified = true;
//     } else if (cursor_y > 0) {
//         // Join with previous line
//         int prev_len = strlen(buffer[cursor_y - 1]);
//         int curr_len = strlen(buffer[cursor_y]);
        
//         if (prev_len + curr_len <= MAX_LINE_LENGTH) {
//             // Copy current line to end of previous line
//             for (int i = 0; i <= curr_len; i++) {
//                 buffer[cursor_y - 1][prev_len + i] = buffer[cursor_y][i];
//             }
            
//             // Shift lines up
//             for (int i = cursor_y; i < num_lines - 1; i++) {
//                 for (int j = 0; j <= MAX_LINE_LENGTH; j++) {
//                     buffer[i][j] = buffer[i + 1][j];
//                 }
//             }
            
//             // Clear last line
//             for (int j = 0; j <= MAX_LINE_LENGTH; j++) {
//                 buffer[num_lines - 1][j] = '\0';
//             }
            
//             cursor_y--;
//             cursor_x = prev_len;
//             num_lines--;
//             modified = true;
//         }
//     }
// }

// // Insert new line
// static void insert_newline(void) {
//     if (num_lines >= MAX_LINES) return;
    
//     // Shift lines down
//     for (int i = num_lines; i > cursor_y; i--) {
//         for (int j = 0; j <= MAX_LINE_LENGTH; j++) {
//             buffer[i][j] = buffer[i - 1][j];
//         }
//     }
    
//     // Split current line
//     int line_len = strlen(buffer[cursor_y]);
//     for (int i = cursor_x; i <= line_len; i++) {
//         buffer[cursor_y + 1][i - cursor_x] = buffer[cursor_y][i];
//         buffer[cursor_y][i] = '\0';
//     }
    
//     cursor_y++;
//     cursor_x = 0;
//     num_lines++;
//     modified = true;
// }

// // Move cursor
// static void move_cursor(int dx, int dy) {
//     if (dy != 0) {
//         cursor_y += dy;
//         if (cursor_y < 0) cursor_y = 0;
//         if (cursor_y >= num_lines) cursor_y = num_lines - 1;
//         if (cursor_y >= MAX_LINES) cursor_y = MAX_LINES - 1;
        
//         // Adjust x position if line is shorter
//         int line_len = strlen(buffer[cursor_y]);
//         if (cursor_x > line_len) cursor_x = line_len;
//     }
    
//     if (dx != 0) {
//         cursor_x += dx;
//         if (cursor_x < 0) cursor_x = 0;
//         int line_len = strlen(buffer[cursor_y]);
//         if (cursor_x > line_len) cursor_x = line_len;
//         if (cursor_x > MAX_LINE_LENGTH) cursor_x = MAX_LINE_LENGTH;
//     }
// }

// // Main editor function
// static void nova_editor(const char* filename) {
//     if (strlen(filename) >= MAX_FILENAME) {
//         kprint("\nError: Filename too long\n", 12);
//         return;
//     }
    
//     strcpy(current_filename, filename);
//     load_file(filename);
    
//     bool running = true;
    
//     while (running) {
//         draw_screen();
        
//         char c = keyboard_getchar();
        
//         // Check for control characters
//         if (c == 24) { // Ctrl+X
//             if (modified) {
//                 clear_screen();
//                 kprint("\nSave changes before exit? (y/n): ", 14);
//                 char response = keyboard_getchar();
//                 if (response == 'y' || response == 'Y') {
//                     if (save_file() == 0) {
//                         kprint("\n\nFile saved successfully!\n", 10);
//                     } else {
//                         kprint("\n\nError saving file!\n", 12);
//                     }
//                 }
//             }
//             running = false;
//         } else if (c == 19) { // Ctrl+S
//             if (save_file() == 0) {
//                 clear_screen();
//                 kprint("\n\nFile saved successfully!\n", 10);
//                 kprint("Press any key to continue...", 7);
//                 keyboard_getchar();
//             } else {
//                 clear_screen();
//                 kprint("\n\nError saving file!\n", 12);
//                 kprint("Press any key to continue...", 7);
//                 keyboard_getchar();
//             }
//         } else if (c == '\n') {
//             insert_newline();
//         } else if (c == '\b') {
//             delete_char();
//         } else if (c == 27) { // ESC - arrow keys start with ESC
//             // For now, we'll use simple WASD-like controls
//             // since arrow keys need escape sequence handling
//         } else if (c >= 32 && c <= 126) { // Printable characters
//             insert_char(c);
//         }
        
//         // Simple arrow key alternatives (without escape sequences)
//         // We'll add proper arrow key support later if needed
//     }
    
//     clear_screen();
// }

// // Main entry point for userspace program
int nova_main(int argc, char** argv) {
    if (argc < 2) {
        kprint("\nUsage: nova <filename>\n\n", 12);
        return 1;
    }
    
    // nova_editor(argv[1]);
    return 0;
}
