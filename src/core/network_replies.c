#include "network_config_internal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    size_t capacity;
    size_t size;
    gs232b_error_t error;
} writer_t;

static void append(writer_t *writer, const char *format, ...)
{
    if (writer->error != GS232B_OK) return;
    if (writer->size >= writer->capacity) {
        writer->error = GS232B_ERROR_BUFFER_TOO_SMALL;
        return;
    }
    va_list args;
    va_start(args, format);
    const int n = vsnprintf((char *)writer->bytes + writer->size,
        writer->capacity - writer->size, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= writer->capacity - writer->size) {
        writer->error = GS232B_ERROR_BUFFER_TOO_SMALL;
    } else writer->size += (size_t)n;
}

static void quoted(writer_t *writer, const char *text)
{
    if (writer->error != GS232B_OK) return;
    if (writer->size >= writer->capacity) {
        writer->error = GS232B_ERROR_BUFFER_TOO_SMALL;
        return;
    }
    size_t size;
    writer->error = gs232b_json_string_encode(
        (gs232b_byte_span_t){(const uint8_t *)text, strlen(text)},
        writer->bytes + writer->size, writer->capacity - writer->size, &size);
    if (writer->error == GS232B_OK) writer->size += size;
}

static const char *boolean(bool value) { return value ? "true" : "false"; }

static void ipv4(writer_t *writer, uint32_t address)
{
    append(writer, "\"%u.%u.%u.%u\"", (unsigned)(address >> 24),
        (unsigned)((address >> 16) & 255), (unsigned)((address >> 8) & 255),
        (unsigned)(address & 255));
}

static void addresses(writer_t *writer, uint32_t address, uint32_t netmask,
    uint32_t gateway, const uint32_t dns[2])
{
    append(writer, "\"address\":"); ipv4(writer, address);
    append(writer, ",\"netmask\":"); ipv4(writer, netmask);
    append(writer, ",\"gateway\":"); ipv4(writer, gateway);
    append(writer, ",\"dns1\":"); ipv4(writer, dns[0]);
    append(writer, ",\"dns2\":"); ipv4(writer, dns[1]);
}

static void config(writer_t *writer, const gs232b_net_config_t *value)
{
    append(writer, "\"eth\":{\"mode\":\"%s\",",
        value->eth.mode == GS232B_ETHERNET_DHCP ? "dhcp" : "static");
    const gs232b_ethernet_ipv4_t *ip = &value->eth.ipv4;
    addresses(writer, ip->address, ip->netmask, ip->gateway, ip->dns);
    append(writer, "},\"wifi\":{\"enabled\":%s,\"ssid\":", boolean(value->wifi.enabled));
    quoted(writer, value->wifi.ssid);
    append(writer, ",\"security\":\"%s\",\"password_set\":%s,\"mode\":\"%s\",\"address\":",
        value->wifi.password[0] == '\0' ? "open" : "wpa2-personal",
        boolean(value->wifi.password[0] != '\0'),
        value->wifi.mode == GS232B_WIFI_STA_STATIC ? "static" : "dhcp");
    ipv4(writer, value->wifi.ipv4.address);
    append(writer, ",\"netmask\":"); ipv4(writer, value->wifi.ipv4.netmask);
    append(writer, ",\"gateway\":"); ipv4(writer, value->wifi.ipv4.gateway);
    append(writer, "}");
}

static uint64_t deadline(const gs232b_config_transaction_t *transaction)
{
    return transaction->renewed_us > UINT64_MAX - GS232B_CONFIG_IDLE_TIMEOUT_US ? UINT64_MAX :
        transaction->renewed_us + GS232B_CONFIG_IDLE_TIMEOUT_US;
}

static const char *eth_state(gs232b_ethernet_state_t state)
{
    static const char *const names[] = {
        "link_down", "link_up_no_ip", "dhcp_wait", "ready", "error"
    };
    return state <= GS232B_ETHERNET_ERROR ? names[state] : "unknown";
}

static const char *wifi_state(gs232b_wifi_sta_state_t state)
{
    static const char *const names[] = {
        "disabled", "starting", "connecting", "dhcp_wait", "ready", "retry_wait", "error"
    };
    return state <= GS232B_WIFI_STA_ERROR ? names[state] : "unknown";
}

static void candidate(writer_t *writer, const gs232b_config_transaction_t *transaction, bool full)
{
    const uint64_t now = transaction->clock.now_us(transaction->clock.ctx);
    if (transaction->candidate_id == 0 || now < transaction->renewed_us ||
        now - transaction->renewed_us >= GS232B_CONFIG_IDLE_TIMEOUT_US) {
        append(writer, "null"); return;
    }
    append(writer, "{\"candidate_id\":%" PRIu32 ",\"base_revision\":%" PRIu64
        ",\"deadline_us\":%" PRIu64 ",\"idle_remaining_us\":%" PRIu64,
        transaction->candidate_id, transaction->base_revision, deadline(transaction),
        GS232B_CONFIG_IDLE_TIMEOUT_US - (now - transaction->renewed_us));
    if (full) { append(writer, ","); config(writer, &transaction->candidate); }
    append(writer, "}");
}

static void trial(writer_t *writer, const gs232b_config_transaction_t *transaction, bool full)
{
    const gs232b_config_trial_t *value = &transaction->trial;
    if (value->state == GS232B_CONFIG_TRIAL_NONE) {
        append(writer, "null");
        return;
    }
    const gs232b_time_us_t now = transaction->clock.now_us(transaction->clock.ctx);
    const uint64_t remaining = value->state == GS232B_CONFIG_TRIAL_ACTIVE &&
        now >= value->started_us && now < value->deadline_us ? value->deadline_us - now : 0;
    const char *state = value->state == GS232B_CONFIG_TRIAL_ACTIVE ? "active" :
        "rollback_pending";
    append(writer, "{\"trial_id\":%" PRIu32 ",\"state\":\"%s\","
        "\"base_revision\":%" PRIu64 ",\"timeout_s\":%" PRIu32
        ",\"deadline_us\":%" PRIu64 ",\"remaining_us\":%" PRIu64,
        value->id, state, value->base_revision, value->timeout_s,
        value->deadline_us, remaining);
    if (full) {
        append(writer, ",\"config\":{");
        config(writer, &value->config);
        append(writer, "}");
    }
    append(writer, "}");
}

static void runtime(writer_t *writer, const gs232b_ethernet_snapshot_t *eth,
    const gs232b_wifi_sta_snapshot_t *wifi)
{
    append(writer, "{\"eth\":{\"state\":\"%s\",\"link_up\":%s,\"ipv4_valid\":%s,",
        eth_state(eth->state), boolean(eth->link_up), boolean(eth->ipv4_valid));
    addresses(writer, eth->current.address, eth->current.netmask, eth->current.gateway, eth->current.dns);
    append(writer, "},\"wifi\":{\"state\":\"%s\",\"enabled\":%s,\"connected\":%s,\"ipv4_valid\":%s,",
        wifi_state(wifi->state), boolean(wifi->enabled), boolean(wifi->connected), boolean(wifi->ipv4_valid));
    addresses(writer, wifi->current.address, wifi->current.netmask, wifi->current.gateway, wifi->current.dns);
    append(writer, ",\"ssid\":"); quoted(writer, wifi->ssid);
    append(writer, "}}");
}

gs232b_error_t gs232b_net_config_encode(const gs232b_config_transaction_t *transaction,
    const gs232b_net_status_port_t *status, uint8_t *data, size_t capacity, size_t *size)
{
    *size = 0;
    gs232b_ethernet_snapshot_t eth = {0};
    gs232b_wifi_sta_snapshot_t wifi = {0};
    if (status->snapshot != NULL) {
        const gs232b_error_t error = status->snapshot(status->ctx, &eth, &wifi);
        if (error != GS232B_OK) return error;
    }
    writer_t writer = {data, capacity, 0, GS232B_OK};
    append(&writer, "{\"active_revision\":%" PRIu64 ",\"active\":{", transaction->active_revision);
    config(&writer, &transaction->active);
    append(&writer, "},\"candidate\":"); candidate(&writer, transaction, true);
    append(&writer, ",\"trial\":"); trial(&writer, transaction, true);
    append(&writer, ",\"runtime\":");
    if (status->snapshot == NULL) append(&writer, "null");
    else runtime(&writer, &eth, &wifi);
    append(&writer, "}");
    if (writer.error == GS232B_OK) *size = writer.size;
    return writer.error;
}

gs232b_error_t gs232b_config_status_encode(const gs232b_config_transaction_t *transaction,
    uint8_t *data, size_t capacity, size_t *size)
{
    *size = 0;
    writer_t writer = {data, capacity, 0, GS232B_OK};
    append(&writer, "{\"active_revision\":%" PRIu64 ",\"candidate\":", transaction->active_revision);
    candidate(&writer, transaction, false);
    append(&writer, ",\"trial\":"); trial(&writer, transaction, false);
    append(&writer, "}");
    if (writer.error == GS232B_OK) *size = writer.size;
    return writer.error;
}
