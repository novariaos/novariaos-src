// SPDX-License-Identifier: GPL-3.0-only

#include <core/arch/rtc.h>
#include <core/kernel/kstd.h>
#include <core/arch/idt.h>
#include <core/arch/io.h>
#include <core/kernel/tty.h>
#include <stddef.h>

static uint8_t read_register(uint8_t reg) {
    outb(RTC_PORT_CMD, reg);
    return inb(RTC_PORT_DATA);
}

static void write_register(uint8_t reg, uint8_t value) {
    outb(RTC_PORT_CMD, reg);
    outb(RTC_PORT_DATA, value);
}

static uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static uint8_t binary_to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

static void wait_ready(void) {
    int timeout = 100000;
    while ((read_register(RTC_STATUS_A) & RTC_UIP) && timeout-- > 0) {
        asm volatile("pause");
    }
}

void rtc_init(void) {
    tty_puts(":: RTC: Initializing...\n");
    
    uint8_t status_b = read_register(RTC_STATUS_B);
    
    char buf[16];
    tty_puts(":: RTC: Status B = 0x");
    itoa(status_b, buf, 16);
    tty_puts(buf);
    tty_puts("\n");
    
    status_b |= RTC_24H;
    status_b &= ~RTC_BINARY;
    
    write_register(RTC_STATUS_B, status_b);
    
    status_b = read_register(RTC_STATUS_B);
    tty_puts(":: RTC: New Status B = 0x");
    itoa(status_b, buf, 16);
    tty_puts(buf);
    tty_puts("\n");
    
    read_register(RTC_STATUS_C);
    
    tty_puts(":: RTC: Initialization complete\n");
}

rtc_time_t rtc_read_time(void) {
    rtc_time_t time;
    
    wait_ready();
    
    uint8_t second = read_register(RTC_SECONDS);
    uint8_t minute = read_register(RTC_MINUTES);
    uint8_t hour = read_register(RTC_HOURS);
    uint8_t weekday = read_register(RTC_WEEKDAY);
    uint8_t day = read_register(RTC_DAY);
    uint8_t month = read_register(RTC_MONTH);
    uint8_t year = read_register(RTC_YEAR);
    uint8_t century = read_register(RTC_CENTURY);
    
    second = bcd_to_binary(second);
    minute = bcd_to_binary(minute);
    hour = bcd_to_binary(hour);
    day = bcd_to_binary(day);
    month = bcd_to_binary(month);
    year = bcd_to_binary(year);
    
    if (century == 0x20 || century == 0x19) {
        century = bcd_to_binary(century);
    } else {
        century = 20;
    }
    
    bool hour24 = (read_register(RTC_STATUS_B) & RTC_24H) != 0;
    if (!hour24) {
        uint8_t hour12 = hour & 0x7F;
        bool is_pm = (hour & 0x80) != 0;
        
        if (is_pm && hour12 < 12) {
            hour = hour12 + 12;
        } else if (!is_pm && hour12 == 12) {
            hour = 0;
        } else {
            hour = hour12;
        }
    }
    
    time.second = second;
    time.minute = minute;
    time.hour = hour;
    time.weekday = weekday;
    time.day = day;
    time.month = month;
    time.year = (century * 100) + year;
    
    return time;
}

uint64_t rtc_read_unix(void) {
    rtc_time_t time = rtc_read_time();
    
    static const uint16_t month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    
    uint64_t timestamp = 0;
    
    for (uint16_t year = 1970; year < time.year; year++) {
        timestamp += 365;
        
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            timestamp++;
        }
    }
    
    for (uint8_t month = 0; month < time.month - 1; month++) {
        timestamp += month_days[month];
        
        if (month == 1) {
            if ((time.year % 4 == 0 && time.year % 100 != 0) || (time.year % 400 == 0)) {
                timestamp++;
            }
        }
    }
    
    timestamp += time.day - 1;
    
    timestamp = timestamp * 86400;
    timestamp += time.hour * 3600;
    timestamp += time.minute * 60;
    timestamp += time.second;
    
    return timestamp;
}

void rtc_set_time(const rtc_time_t *time) {
    wait_ready();
    
    uint8_t second = binary_to_bcd(time->second);
    uint8_t minute = binary_to_bcd(time->minute);
    uint8_t hour = binary_to_bcd(time->hour);
    uint8_t day = binary_to_bcd(time->day);
    uint8_t month = binary_to_bcd(time->month);
    uint8_t year = binary_to_bcd(time->year % 100);
    uint8_t century = binary_to_bcd(time->year / 100);
    
    write_register(RTC_SECONDS, second);
    write_register(RTC_MINUTES, minute);
    write_register(RTC_HOURS, hour);
    write_register(RTC_DAY, day);
    write_register(RTC_MONTH, month);
    write_register(RTC_YEAR, year);
    write_register(RTC_CENTURY, century);
    write_register(RTC_WEEKDAY, time->weekday);
}

void rtc_set_unix(uint64_t timestamp) {
    rtc_time_t time;
    uint64_t days = timestamp / 86400;
    uint64_t seconds_remain = timestamp % 86400;
    
    time.hour = seconds_remain / 3600;
    time.minute = (seconds_remain % 3600) / 60;
    time.second = seconds_remain % 60;
    
    uint16_t year = 1970;
    while (days >= 365) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        uint16_t days_in_year = leap ? 366 : 365;
        
        if (days >= days_in_year) {
            days -= days_in_year;
            year++;
        } else {
            break;
        }
    }
    
    static const uint16_t month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    uint8_t month = 0;
    
    for (month = 0; month < 12; month++) {
        uint16_t days_in_month = month_days[month];
        
        if (month == 1 && leap) {
            days_in_month = 29;
        }
        
        if (days >= days_in_month) {
            days -= days_in_month;
        } else {
            break;
        }
    }
    
    time.year = year;
    time.month = month + 1;
    time.day = days + 1;
    
    uint64_t total_days = timestamp / 86400;
    time.weekday = (total_days + 4) % 7 + 1;
    
    rtc_set_time(&time);
}

void rtc_enable_periodic_interrupt(uint32_t hz) {
    uint8_t rate = 0;
    
    if (hz >= 8192) rate = 1;
    else if (hz >= 4096) rate = 2;
    else if (hz >= 2048) rate = 3;
    else if (hz >= 1024) rate = 4;
    else if (hz >= 512) rate = 5;
    else if (hz >= 256) rate = 6;
    else if (hz >= 128) rate = 7;
    else if (hz >= 64) rate = 8;
    else if (hz >= 32) rate = 9;
    else if (hz >= 16) rate = 10;
    else if (hz >= 8) rate = 11;
    else if (hz >= 4) rate = 12;
    else if (hz >= 2) rate = 13;
    else rate = 14;
    
    uint8_t status_a = read_register(RTC_STATUS_A);
    status_a = (status_a & 0xF0) | rate;
    write_register(RTC_STATUS_A, status_a);
    
    uint8_t status_b = read_register(RTC_STATUS_B);
    status_b |= RTC_PIE;
    write_register(RTC_STATUS_B, status_b);
}

void rtc_disable_periodic_interrupt(void) {
    uint8_t status_b = read_register(RTC_STATUS_B);
    status_b &= ~RTC_PIE;
    write_register(RTC_STATUS_B, status_b);
}

uint8_t rtc_handle_interrupt(void) {
    uint8_t status_c = read_register(RTC_STATUS_C);
    return (status_c & 0x70) != 0;
}

const char* rtc_get_weekday_string(uint8_t weekday) {
    static const char* days[] = {
        "Monday", "Tuesday", "Wednesday", "Thursday",
        "Friday", "Saturday", "Sunday"
    };
    
    if (weekday < 1 || weekday > 7) return "Unknown";
    return days[weekday - 1];
}

bool rtc_is_update_in_progress(void) {
    return (read_register(RTC_STATUS_A) & RTC_UIP) != 0;
}