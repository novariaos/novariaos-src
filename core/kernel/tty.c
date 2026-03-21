// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/tty.h>

void tty_register_driver(tty_driver_t* driver) {
    current_driver = driver;
    
    if (driver && driver->clear) {
        driver->clear();
    }
}

void tty_puts(const char* str) {
    if (!current_driver || !current_driver->puts) {
        return;
    }
    
    for (const char* c = str; *c; c++) {
        unsigned char ch = *c;
        
        if (escape_state == 0) {
            if (ch == '\033') {
                escape_state = 1;
                current_param = 0;
                param_count = 0;
            } else {
                int color_code = (current_bg << 4) | current_fg;
                current_driver->puts((const char[]){ch, '\0'}, color_code);
            }
        } 
        else if (escape_state == 1) {
            if (ch == '[') {
                escape_state = 2;
            } else {
                escape_state = 0;
            }
        } 
        else if (escape_state == 2) {
            if (ch >= '0' && ch <= '9') {
                current_param = current_param * 10 + (ch - '0');
            } 
            else if (ch == ';') {
                if (param_count < 16) {
                    params[param_count++] = current_param;
                }
                current_param = 0;
            } 
            else if (ch == 'm') {
                if (param_count < 16) {
                    params[param_count++] = current_param;
                }
                
                for (int i = 0; i < param_count; i++) {
                    int p = params[i];
                    
                    if (p >= 30 && p <= 37) {
                        current_fg = p - 30;
                    }
                    else if (p >= 40 && p <= 47) {
                        current_bg = p - 40;
                    }
                    else if (p >= 90 && p <= 97) {
                        current_fg = p - 90 + 8;
                    }
                    else if (p >= 100 && p <= 107) {
                        current_bg = p - 100 + 8;
                    }
                    else if (p == 0) {
                        current_fg = 7;
                        current_bg = 0;
                    }
                    else if (p == 39) {
                        current_fg = 7;
                    }
                    else if (p == 49) {
                        current_bg = 0;
                    }
                }
                
                escape_state = 0;
                current_param = 0;
                param_count = 0;
            } 
            else {
                escape_state = 0;
                current_param = 0;
                param_count = 0;
            }
        }
    }
}

void tty_clear() {
    if (current_driver && current_driver->clear) {
        current_driver->clear();
    }
}