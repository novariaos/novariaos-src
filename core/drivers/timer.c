// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/timer.h>
#include <core/arch/rtc.h>
#include <core/arch/io.h>
#include <core/kernel/kstd.h>
#include <core/kernel/tty.h>
#include <string.h>

#define MAX_TIMERS 8

static timer_driver_t* timers[MAX_TIMERS];
static int timer_count = 0;
static timer_driver_t* active_timer = NULL;
static bool timer_initialized = false;

static uint64_t rtc_adapter_get_time(void) {
    if (!timer_initialized) return 0;
    return rtc_read_unix();
}

static uint64_t rtc_adapter_get_uptime(void) {
    if (!timer_initialized) return 0;
    return 0;
}

static uint64_t rtc_adapter_get_unix_epoch(void) {
    if (!timer_initialized) return 0;
    return rtc_read_unix();
}

static void rtc_adapter_get_readable(char* buf, size_t size) {
    if (!buf || size < 2) return;
    
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
    
    buf[size - 1] = '\0';
}

static void rtc_adapter_init(void) {
    tty_puts(":: Initializing RTC hardware...\n");
    rtc_init();
    tty_puts(":: RTC hardware initialized\n");
}

static void rtc_adapter_enable_interrupt(uint32_t hz) {
    rtc_enable_periodic_interrupt(hz);
}

static void rtc_adapter_disable_interrupt(void) {
    rtc_disable_periodic_interrupt();
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

static void timer_register(timer_driver_t* driver) {
    if (!driver || timer_count >= MAX_TIMERS) {
        return;
    }
    
    timers[timer_count] = driver;
    timer_count++;
}

void init_timer_subsystem(void) {
    tty_puts(":: Initializing timer subsystem...\n");
    
    timer_register(&rtc_timer_driver);
    
    for (int i = 0; i < timer_count; i++) {
        tty_puts(":: Initializing timer: ");
        tty_puts(timers[i]->get_name ? timers[i]->get_name() : timers[i]->name ? timers[i]->name : "unknown");
        tty_puts("\n");
        
        if (timers[i]->init) {
            timers[i]->init();
        }
        
        tty_puts(":: Registered timer: ");
        tty_puts(timers[i]->get_name ? timers[i]->get_name() : timers[i]->name ? timers[i]->name : "unknown");
        tty_puts("\n");
    }
    
    if (timer_count > 0) {
        active_timer = timers[0];
        
        const char* timer_name = active_timer->get_name ? 
                                 active_timer->get_name() : 
                                 active_timer->name ? active_timer->name : "unknown";
        
        tty_puts(":: Selected default timer: ");
        tty_puts(timer_name);
        tty_puts("\n");
    } else {
        tty_puts(":: ERROR: No timers registered!\n");
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
    if (!timer_initialized || !active_timer) {
        return "Timer not initialized";
    }
    if (active_timer->get_name) {
        return active_timer->get_name();
    }
    return active_timer->name ? active_timer->name : "Unknown timer";
}