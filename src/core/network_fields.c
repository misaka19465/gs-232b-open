#include "network_config_internal.h"

#include <string.h>

static const gs232b_json_field_t eth_fields[] = {
    {.name = "mode", .type = GS232B_JSON_STRING, .max_bytes = 6},
    {.name = "address", .type = GS232B_JSON_STRING, .max_bytes = 15},
    {.name = "netmask", .type = GS232B_JSON_STRING, .max_bytes = 15},
    {.name = "gateway", .type = GS232B_JSON_STRING, .max_bytes = 15},
    {.name = "dns1", .type = GS232B_JSON_STRING, .max_bytes = 15},
    {.name = "dns2", .type = GS232B_JSON_STRING, .max_bytes = 15}
};
static const gs232b_json_field_t wifi_fields[] = {
    {.name = "enabled", .type = GS232B_JSON_BOOL},
    {.name = "ssid", .type = GS232B_JSON_STRING, .max_bytes = GS232B_WIFI_STA_SSID_MAX},
    {.name = "password", .type = GS232B_JSON_STRING, .max_bytes = GS232B_WIFI_STA_PASSWORD_MAX},
    {.name = "security", .type = GS232B_JSON_STRING, .max_bytes = 13},
    {.name = "mode", .type = GS232B_JSON_STRING, .max_bytes = 6},
    {.name = "address", .type = GS232B_JSON_STRING, .max_bytes = 15},
    {.name = "netmask", .type = GS232B_JSON_STRING, .max_bytes = 15},
    {.name = "gateway", .type = GS232B_JSON_STRING, .max_bytes = 15}
};
static const gs232b_json_schema_t eth_schema = {eth_fields, sizeof(eth_fields) / sizeof(eth_fields[0])};
static const gs232b_json_schema_t wifi_schema = {wifi_fields, sizeof(wifi_fields) / sizeof(wifi_fields[0])};
static const gs232b_json_field_t net_fields[] = {
    {.name = "eth", .type = GS232B_JSON_OBJECT, .object = &eth_schema},
    {.name = "wifi", .type = GS232B_JSON_OBJECT, .object = &wifi_schema}
};
const gs232b_json_schema_t gs232b_net_patch_schema = {net_fields, sizeof(net_fields) / sizeof(net_fields[0])};

static gs232b_error_t string(gs232b_json_value_t value, char *out, size_t capacity)
{
    size_t size;
    const gs232b_error_t error = gs232b_json_string_decode(value,
        (uint8_t *)out, capacity - 1U, &size);
    if (error != GS232B_OK || memchr(out, 0, size) != NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    out[size] = '\0';
    return GS232B_OK;
}

static bool unicast(uint32_t address)
{
    const uint32_t first = address >> 24;
    return first != 0 && first != 127 && first < 224;
}

static bool mask_valid(uint32_t mask)
{
    const uint32_t suffix = ~mask;
    return mask != 0 && suffix >= 3 && (suffix & (suffix + 1U)) == 0;
}

static gs232b_error_t patch_eth(gs232b_json_value_t object, gs232b_ethernet_config_t *eth)
{
    gs232b_json_value_t value;
    char text[16];
    if (gs232b_json_get(object, "mode", &value) == GS232B_OK) {
        if (string(value, text, sizeof(text)) != GS232B_OK) return GS232B_ERROR_INVALID_ARGUMENT;
        if (strcmp(text, "dhcp") == 0) eth->mode = GS232B_ETHERNET_DHCP;
        else if (strcmp(text, "static") == 0) eth->mode = GS232B_ETHERNET_STATIC;
        else return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const char *const names[] = {"address", "netmask", "gateway", "dns1", "dns2"};
    uint32_t *const fields[] = {&eth->ipv4.address, &eth->ipv4.netmask,
        &eth->ipv4.gateway, &eth->ipv4.dns[0], &eth->ipv4.dns[1]};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (gs232b_json_get(object, names[i], &value) != GS232B_OK) continue;
        uint32_t address;
        if (string(value, text, sizeof(text)) != GS232B_OK ||
            gs232b_ethernet_parse_ipv4(text, &address) != GS232B_OK ||
            (i == 1 ? !mask_valid(address) :
                !((i >= 2 && address == 0) || unicast(address)))) return GS232B_ERROR_INVALID_ARGUMENT;
        *fields[i] = address;
    }
    /* NOTE: 可分多次补齐静态字段；已能判定的主机/网关冲突必须立即拒绝。 */
    if (eth->ipv4.address != 0 && eth->ipv4.netmask != 0) {
        gs232b_ethernet_config_t check = *eth;
        check.mode = GS232B_ETHERNET_STATIC;
        if (!gs232b_ethernet_config_valid(&check)) return GS232B_ERROR_INVALID_ARGUMENT;
    } else if (eth->ipv4.gateway != 0 && eth->ipv4.gateway == eth->ipv4.address) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return GS232B_OK;
}

static bool placeholder(const char *password)
{
    size_t i = 0;
    while (password[i] == '*') ++i;
    if (i != 0 && password[i] == '\0') return true;
    char lower[GS232B_WIFI_STA_PASSWORD_MAX + 1U];
    for (i = 0; password[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)password[i];
        lower[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    lower[i] = '\0';
    return strcmp(lower, "<redacted>") == 0 || strcmp(lower, "[redacted]") == 0 ||
        strcmp(lower, "redacted") == 0;
}

static gs232b_error_t patch_wifi(gs232b_json_value_t object, gs232b_wifi_sta_config_t *wifi)
{
    gs232b_json_value_t value;
    /* NOTE: 地址规则共用校验，实际字段仍由 STA 与 ETH 独立持有。 */
    gs232b_ethernet_config_t ip = {
        .mode = wifi->mode == GS232B_WIFI_STA_STATIC ? GS232B_ETHERNET_STATIC : GS232B_ETHERNET_DHCP,
        .ipv4 = {.address = wifi->ipv4.address, .netmask = wifi->ipv4.netmask,
            .gateway = wifi->ipv4.gateway}
    };
    if (patch_eth(object, &ip) != GS232B_OK) return GS232B_ERROR_INVALID_ARGUMENT;
    wifi->mode = ip.mode == GS232B_ETHERNET_STATIC ? GS232B_WIFI_STA_STATIC : GS232B_WIFI_STA_DHCP;
    wifi->ipv4 = (gs232b_wifi_sta_address_t){ip.ipv4.address, ip.ipv4.netmask, ip.ipv4.gateway};
    if (gs232b_json_get(object, "enabled", &value) == GS232B_OK) {
        wifi->enabled = gs232b_byte_span_equal_ascii_ci(value.raw, "true");
    }
    if (gs232b_json_get(object, "security", &value) == GS232B_OK) {
        char security[14];
        if (string(value, security, sizeof(security)) != GS232B_OK ||
            strcmp(security, "wpa2-personal") != 0) return GS232B_ERROR_INVALID_ARGUMENT;
    }
    if (gs232b_json_get(object, "ssid", &value) == GS232B_OK &&
        string(value, wifi->ssid, sizeof(wifi->ssid)) != GS232B_OK) return GS232B_ERROR_INVALID_ARGUMENT;
    if (gs232b_json_get(object, "password", &value) == GS232B_OK) {
        if (string(value, wifi->password, sizeof(wifi->password)) != GS232B_OK ||
            placeholder(wifi->password)) return GS232B_ERROR_INVALID_ARGUMENT;
        if (wifi->password[0] != '\0') {
            gs232b_wifi_sta_config_t check = *wifi;
            memcpy(check.ssid, "check", sizeof("check"));
            check.mode = GS232B_WIFI_STA_DHCP;
            if (!gs232b_wifi_sta_config_valid(&check)) return GS232B_ERROR_INVALID_ARGUMENT;
        }
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_net_config_patch(const gs232b_net_config_t *base,
    gs232b_json_value_t arguments, gs232b_net_config_t *candidate)
{
    if (base == NULL || candidate == NULL ||
        gs232b_json_validate_object(arguments, &gs232b_net_patch_schema) != GS232B_OK) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_net_config_t next = *base;
    gs232b_json_value_t object;
    gs232b_error_t error = GS232B_OK;
    if (gs232b_json_get(arguments, "eth", &object) == GS232B_OK) error = patch_eth(object, &next.eth);
    if (error == GS232B_OK && gs232b_json_get(arguments, "wifi", &object) == GS232B_OK) {
        error = patch_wifi(object, &next.wifi);
    }
    if (error == GS232B_OK) *candidate = next;
    return error;
}
