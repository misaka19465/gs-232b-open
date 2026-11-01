#include "gs232b/ethernet.h"

#include <stddef.h>

gs232b_error_t gs232b_ethernet_parse_ipv4(const char *text, uint32_t *address)
{
    if (address == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    *address = 0;
    if (text == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    uint32_t result = 0;
    for (unsigned part = 0; part < 4; ++part) {
        const char *start = text;
        unsigned value = 0, digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10U + (unsigned)(*text++ - '0');
            if (++digits > 3 || value > 255) { return GS232B_ERROR_INVALID_ARGUMENT; }
        }
        if (digits == 0 || (digits > 1 && *start == '0') ||
            (part < 3 ? *text != '.' : *text != '\0')) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
        result = (result << 8) | value;
        if (part < 3) { ++text; }
    }
    *address = result;
    return GS232B_OK;
}

static bool unicast(uint32_t address)
{
    const uint32_t first = address >> 24;
    return first != 0 && first != 127 && first < 224;
}

static bool host(uint32_t address, uint32_t mask)
{
    const uint32_t suffix = address & ~mask;
    return unicast(address) && suffix != 0 && suffix != ~mask;
}

bool gs232b_ethernet_config_valid(const gs232b_ethernet_config_t *config)
{
    if (config == NULL) { return false; }
    const gs232b_ethernet_ipv4_t *ip = &config->ipv4;
    if (config->mode == GS232B_ETHERNET_DHCP) {
        return (ip->address | ip->netmask | ip->gateway | ip->dns[0] | ip->dns[1]) == 0;
    }
    if (config->mode != GS232B_ETHERNET_STATIC) { return false; }
    const uint32_t suffix = ~ip->netmask;
    if (ip->netmask == 0 || suffix < 3 || (suffix & (suffix + 1U)) != 0 ||
        !host(ip->address, ip->netmask)) { return false; }
    if (ip->gateway != 0 &&
        (!host(ip->gateway, ip->netmask) || ip->gateway == ip->address ||
         (ip->gateway & ip->netmask) != (ip->address & ip->netmask))) { return false; }
    return (ip->dns[0] == 0 || unicast(ip->dns[0])) &&
           (ip->dns[1] == 0 || unicast(ip->dns[1]));
}
