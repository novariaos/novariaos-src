#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>

#define RTC_PORT_DATA   0x71
#define RTC_PORT_CMD    0x70

#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_WEEKDAY     0x06
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_CENTURY     0x32

#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B
#define RTC_STATUS_C    0x0C
#define RTC_STATUS_D    0x0D

#define RTC_UIP         (1 << 7)
#define RTC_24H         (1 << 1)
#define RTC_BINARY      (1 << 2)
#define RTC_PIE         (1 << 6)

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
} rtc_time_t;

void rtc_init(void);

rtc_time_t rtc_read_time(void);
uint64_t rtc_read_unix(void);

void rtc_set_time(const rtc_time_t *time);
void rtc_set_unix(uint64_t timestamp);

void rtc_enable_periodic_interrupt(uint32_t hz);
void rtc_disable_periodic_interrupt(void);
uint8_t rtc_handle_interrupt(void);

const char* rtc_get_weekday_string(uint8_t weekday);
bool rtc_is_update_in_progress(void);

#endif // RTC_H