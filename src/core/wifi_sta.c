#include "gs232b/wifi_sta.h"

#include <stddef.h>
#include <string.h>

static bool utf8(const unsigned char *p, size_t size)
{
    for (size_t i = 0; i < size;) {
        const unsigned char first = p[i++];
        if (first < 0x80) { continue; }
        unsigned count;
        uint32_t value, minimum;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; value = first & 0xf; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; value = first & 7; minimum = 0x10000; }
        else { return false; }
        if (size - i < count) { return false; }
        while (count-- != 0) {
            if ((p[i] & 0xc0) != 0x80) { return false; }
            value = (value << 6) | (p[i++] & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

bool gs232b_wifi_sta_config_valid(const gs232b_wifi_sta_config_t *config)
{
    if (config == NULL || memchr(config->ssid, 0, sizeof(config->ssid)) == NULL ||
        memchr(config->password, 0, sizeof(config->password)) == NULL) { return false; }
    const size_t ssid = strlen(config->ssid), password = strlen(config->password);
    if (ssid == 0 && password == 0) { return !config->enabled; }
    if (ssid == 0 || !utf8((const unsigned char *)config->ssid, ssid) || password < 8) {
        return false;
    }
    if (password < 64) {
        for (size_t i = 0; i < password; ++i) {
            const unsigned char c = (unsigned char)config->password[i];
            if (c < 0x20 || c > 0x7e) { return false; }
        }
        return true;
    }
    for (size_t i = 0; i < password; ++i) {
        const char c = config->password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) { return false; }
    }
    return true;
}

gs232b_time_us_t gs232b_wifi_sta_retry_delay(uint32_t consecutive_failures)
{
    if (consecutive_failures == 0) { return 0; }
    if (consecutive_failures >= 6) { return GS232B_WIFI_STA_RETRY_MAX_US; }
    return UINT64_C(1000000) << (consecutive_failures - 1U);
}
