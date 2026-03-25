// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/timer.h>
#include <core/arch/rtc.h>
#include <core/arch/apic.h>
#include <core/arch/io.h>
#include <core/kernel/kstd.h>
#include <core/kernel/tty.h>
#include <string.h>

#define MAX_TIMERS 8

typedef struct {
    timer_driver_t* driver;
    bool initialized;
    int priority;  // Lower = better
} timer_entry_t;

static timer_entry_t timers[MAX_TIMERS];
static int timer_count = 0;
static timer_driver_t* active_timer = NULL;
static bool timer_initialized = false;
static bool rtc_hw_initialized = false;

static uint64_t rtc_adapter_get_time(void) {
    if (!rtc_hw_initialized) return 0;
    return rtc_read_unix();
}

static uint64_t rtc_adapter_get_uptime(void) {
    if (apic_available()) {
        return apic_get_uptime_ms();
    }
    return 0;
}

static uint64_t rtc_adapter_get_unix_epoch(void) {
    if (!rtc_hw_initialized) return 0;
    return rtc_read_unix();
}

static void rtc_adapter_get_readable(char* buf, size_t size) {
    if (!buf || size < 2) {
        if (buf && size > 0) buf[0] = '\0';
        return;
    }
    
    if (!rtc_hw_initialized) {
        const char* err = "RTC not initialized";
        size_t len = strlen(err);
        if (len >= size) len = size - 1;
        memcpy(buf, err, len);
        buf[len] = '\0';
        return;
    }
    
    rtc_time_t time = rtc_read_time();
    
    char temp[32];
    size_t pos = 0;
    
    const char* day_str = rtc_get_weekday_string(time.weekday);
    while (*day_str && pos < size - 1) {
        buf[pos++] = *day_str++;
    }
    if (pos < size - 1) buf[pos++] = ' ';
    
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char* month_str = months[time.month - 1];
    while (*month_str && pos < size - 1) {
        buf[pos++] = *month_str++;
    }
    if (pos < size - 1) buf[pos++] = ' ';
    
    itoa(time.day, temp, 10);
    char* tp = temp;
    while (*tp && pos < size - 1) {
        buf[pos++] = *tp++;
    }
    if (pos < size - 1) buf[pos++] = ' ';
    
    if (time.hour < 10 && pos < size - 1) buf[pos++] = '0';
    itoa(time.hour, temp, 10);
    tp = temp;
    while (*tp && pos < size - 1) {
        buf[pos++] = *tp++;
    }
    if (pos < size - 1) buf[pos++] = ':';
    
    if (time.minute < 10 && pos < size - 1) buf[pos++] = '0';
    itoa(time.minute, temp, 10);
    tp = temp;
    while (*tp && pos < size - 1) {
        buf[pos++] = *tp++;
    }
    if (pos < size - 1) buf[pos++] = ':';
    
    if (time.second < 10 && pos < size - 1) buf[pos++] = '0';
    itoa(time.second, temp, 10);
    tp = temp;
    while (*tp && pos < size - 1) {
        buf[pos++] = *tp++;
    }
    
    if (pos < size - 1) buf[pos++] = ' ';
    itoa(time.year, temp, 10);
    tp = temp;
    while (*tp && pos < size - 1) {
        buf[pos++] = *tp++;
    }
    
    buf[pos] = '\0';
}

static void rtc_adapter_init(void) {
    tty_puts(":: Initializing RTC hardware...\n");
    rtc_init();
    rtc_hw_initialized = true;  // Assume success, or check internal state if available
    tty_puts(":: RTC hardware initialized\n");
}

static void rtc_adapter_enable_interrupt(uint32_t hz) {
    if (rtc_hw_initialized) {
        rtc_enable_periodic_interrupt(hz);
    }
}

static void rtc_adapter_disable_interrupt(void) {
    if (rtc_hw_initialized) {
        rtc_disable_periodic_interrupt();
    }
}

static const char* rtc_adapter_get_name(void) {
    return "Real-Time Clock (CMOS)";
}

static timer_driver_t rtc_timer_driver = {
    .get_time = rtc_adapter_get_time,
    .get_uptime = rtc_adapter_get_uptime,
    .get_unix_epoch = rtc_adapter_get_unix_epoch,
    .get_readable = rtc_adapter_get_readable,
    .init = rtc_adapter_init,
    .enable_interrupt = rtc_adapter_enable_interrupt,
    .disable_interrupt = rtc_adapter_disable_interrupt,
    .get_name = rtc_adapter_get_name,
    .name = "rtc"
};

static bool timer_register(timer_driver_t* driver, int priority) {
    if (!driver || timer_count >= MAX_TIMERS) {
        return false;
    }
    
    timers[timer_count].driver = driver;
    timers[timer_count].priority = priority;
    timers[timer_count].initialized = false;
    timer_count++;
    return true;
}

static void timer_select_best(void) {
    int best_idx = -1;
    int best_priority = 1000;
    
    for (int i = 0; i < timer_count; i++) {
        if (timers[i].initialized && timers[i].driver) {
            int priority_adj = timers[i].priority;
            if (!timers[i].driver->get_uptime) {
                priority_adj += 100;
            }
            
            if (priority_adj < best_priority) {
                best_priority = priority_adj;
                best_idx = i;
            }
        }
    }
    
    if (best_idx >= 0) {
        active_timer = timers[best_idx].driver;
    }
}
void init_timer_subsystem(void) {
    tty_puts(":: Initializing timer subsystem...\n");
    
    timer_register(&rtc_timer_driver, 10);
    
    bool any_success = false;
    
    for (int i = 0; i < timer_count; i++) {
        tty_puts(":: Initializing timer: ");
        const char* name = timers[i].driver->get_name ? 
                           timers[i].driver->get_name() : 
                           timers[i].driver->name ? timers[i].driver->name : "unknown";
        tty_puts(name);
        tty_puts("\n");
        
        if (timers[i].driver->init) {
            timers[i].driver->init();
            timers[i].initialized = true;
            any_success = true;
        }
        
        tty_puts(":: Registered timer: ");
        tty_puts(name);
        tty_puts("\n");
    }
    
    if (any_success) {
        timer_select_best();
        
        if (active_timer) {
            const char* timer_name = active_timer->get_name ? 
                                     active_timer->get_name() : 
                                     active_timer->name ? active_timer->name : "unknown";
            
            tty_puts(":: Selected default timer: ");
            tty_puts(timer_name);
            tty_puts("\n");
        }
    } else {
        tty_puts(":: ERROR: No timers initialized successfully!\n");
        active_timer = NULL;
    }
    
    timer_initialized = true;
    tty_puts(":: Timer subsystem ready\n");
}

uint64_t timer_get_uptime(void) {
    if (!timer_initialized || !active_timer || !active_timer->get_uptime) {
        return 0;
    }
    return active_timer->get_uptime();
}

uint64_t timer_get_time(void) {
    if (!timer_initialized || !active_timer || !active_timer->get_time) {
        return 0;
    }
    return active_timer->get_time();
}

uint64_t timer_get_unix_epoch(void) {
    if (!timer_initialized || !active_timer) {
        return 0;
    }
    if (active_timer->get_unix_epoch) {
        return active_timer->get_unix_epoch();
    }
    return timer_get_time();
}

void timer_get_readable(char* buf, size_t size) {
    if (!buf || size == 0) {
        return;
    }
    
    memset(buf, 0, size);
    
    if (!timer_initialized || !active_timer) {
        const char* err = "Timer not initialized";
        size_t len = strlen(err);
        if (len >= size) len = size - 1;
        memcpy(buf, err, len);
        buf[len] = '\0';
        return;
    }
    
    if (active_timer->get_readable) {
        active_timer->get_readable(buf, size);
    } else {
        const char* err = "No readable function";
        size_t len = strlen(err);
        if (len >= size) len = size - 1;
        memcpy(buf, err, len);
        buf[len] = '\0';
    }
    
    buf[size - 1] = '\0';
}

const char* timer_get_name(void) {
    static const char* not_initialized = "Timer not initialized";
    static const char* unknown = "Unknown timer";
    
    if (!timer_initialized || !active_timer) {
        return not_initialized;
    }
    if (active_timer->get_name) {
        return active_timer->get_name();
    }
    return active_timer->name ? active_timer->name : unknown;
}