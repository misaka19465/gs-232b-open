#include "gs232b/provisioning.h"
#include "gs232b/extension_json.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t capacity, size;
    gs232b_error_t error;
} scan_writer_t;

static void append(scan_writer_t *writer, const char *format, ...)
{
    if (writer->error != GS232B_OK) return;
    if (writer->size >= writer->capacity) {
        writer->error = GS232B_ERROR_BUFFER_TOO_SMALL;
        return;
    }
    va_list args;
    va_start(args, format);
    const int n = vsnprintf((char *)writer->data + writer->size,
        writer->capacity - writer->size, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= writer->capacity - writer->size) {
        writer->error = GS232B_ERROR_BUFFER_TOO_SMALL;
    } else writer->size += (size_t)n;
}

static gs232b_error_t refresh(gs232b_provisioning_t *provisioning)
{
    gs232b_provisioning_scan_t *scan = &provisioning->scan;
    scan->valid = false;
    if (provisioning->ap_port.scan == NULL) return GS232B_ERROR_NOT_READY;
    if (scan->id == UINT32_MAX) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    size_t count = 0;
    bool truncated = false;
    const gs232b_error_t error = provisioning->ap_port.scan(provisioning->ap_port.ctx,
        scan->aps, GS232B_PROVISIONING_SCAN_MAX_APS, &count, &truncated);
    if (error != GS232B_OK) return error;
    if (count > GS232B_PROVISIONING_SCAN_MAX_APS) return GS232B_ERROR_CORRUPT;
    scan->count = 0;
    for (size_t i = 0; i < count; ++i) {
        gs232b_wifi_sta_config_t network = {.enabled = true};
        memcpy(network.ssid, scan->aps[i].ssid, sizeof(network.ssid));
        if (!gs232b_wifi_sta_config_valid(&network)) continue;
        scan->aps[scan->count++] = scan->aps[i];
    }
    for (size_t i = 1; i < scan->count; ++i) {
        const gs232b_provisioning_ap_t ap = scan->aps[i];
        size_t at = i;
        while (at != 0 && scan->aps[at - 1U].rssi < ap.rssi) {
            scan->aps[at] = scan->aps[at - 1U];
            --at;
        }
        scan->aps[at] = ap;
    }
    ++scan->id;
    scan->created_us = provisioning->clock.now_us(provisioning->clock.ctx);
    scan->truncated = truncated;
    scan->valid = true;
    return GS232B_OK;
}

gs232b_error_t gs232b_provisioning_scan_page(gs232b_provisioning_t *provisioning,
    uint32_t scan_id, uint32_t cursor, uint8_t *data, size_t capacity, size_t *size)
{
    if (size == NULL || (capacity != 0 && data == NULL)) return GS232B_ERROR_INVALID_ARGUMENT;
    *size = 0;
    if (provisioning == NULL || !provisioning->initialized ||
        provisioning->status.state != GS232B_PROVISIONING_ACTIVE) return GS232B_ERROR_NOT_READY;
    if (scan_id == 0 && cursor != 0) return GS232B_ERROR_INVALID_ARGUMENT;
    if (scan_id == 0) {
        const gs232b_error_t error = refresh(provisioning);
        if (error != GS232B_OK) return error;
    }
    const gs232b_provisioning_scan_t *scan = &provisioning->scan;
    const gs232b_time_us_t now = provisioning->clock.now_us(provisioning->clock.ctx);
    if (!scan->valid || (scan_id != 0 && scan_id != scan->id) || now < scan->created_us ||
        now - scan->created_us >= GS232B_PROVISIONING_SCAN_TTL_US) return GS232B_ERROR_CONFLICT;
    if (cursor > scan->count) return GS232B_ERROR_INVALID_ARGUMENT;
    const size_t end = scan->count - cursor > GS232B_PROVISIONING_SCAN_PAGE_APS ?
        cursor + GS232B_PROVISIONING_SCAN_PAGE_APS : scan->count;
    scan_writer_t writer = {data, capacity, 0, GS232B_OK};
    append(&writer, "{\"scan_id\":%" PRIu32 ",\"offset\":%" PRIu32 ",\"total\":%u,\"next\":",
        scan->id, cursor, (unsigned)scan->count);
    if (end == scan->count) append(&writer, "null");
    else append(&writer, "%u", (unsigned)end);
    append(&writer, ",\"truncated\":%s,\"aps\":[", scan->truncated ? "true" : "false");
    for (size_t i = cursor; i < end && writer.error == GS232B_OK; ++i) {
        const gs232b_provisioning_ap_t *ap = &scan->aps[i];
        append(&writer, "%s{\"ssid\":", i == cursor ? "" : ",");
        if (writer.error != GS232B_OK) break;
        size_t quoted_size = 0;
        /* NOTE: 使用协议共用编码器，保留中文并正确转义控制字符；单块最坏长度仍小于 2048。 */
        writer.error = gs232b_json_string_encode(
            (gs232b_byte_span_t){(const uint8_t *)ap->ssid, strlen(ap->ssid)},
            writer.data + writer.size, writer.capacity - writer.size, &quoted_size);
        if (writer.error != GS232B_OK) break;
        writer.size += quoted_size;
        append(&writer, ",\"rssi\":%d,\"auth\":%u}", ap->rssi, ap->auth);
    }
    append(&writer, "]}");
    if (writer.error == GS232B_OK) *size = writer.size;
    return writer.error;
}
