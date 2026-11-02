/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "gs232b/extension.h"

#include <string.h>

static const uint8_t empty_object[] = "{}";

static uint8_t upper(uint8_t c)
{
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}

static bool letter(uint8_t c)
{
    return upper(c) >= 'A' && upper(c) <= 'Z';
}

static bool name_byte(uint8_t c)
{
    return letter(c) || (c >= '0' && c <= '9') || c == '_';
}

static bool name_copy(char *out, const uint8_t *name, size_t size)
{
    if (name == NULL || size == 0 || size > GS232B_EXTENSION_NAME_MAX || !letter(name[0])) return false;
    for (size_t i = 0; i < size; ++i) {
        if (!name_byte(name[i])) return false;
        out[i] = (char)upper(name[i]);
    }
    out[size] = '\0';
    return true;
}

static bool command_valid(const char *cmd)
{
    size_t n = 0;
    while (n < GS232B_EXTENSION_COMMAND_BYTES && cmd[n] != '\0') ++n;
    if (n == 0) return true;
    if (n >= GS232B_EXTENSION_COMMAND_BYTES) return false;
    size_t space = 0;
    while (space < n && cmd[space] != ' ') ++space;
    char name[GS232B_EXTENSION_NAME_MAX + 1U];
    return space < n && name_copy(name, (const uint8_t *)cmd, space) &&
        name_copy(name, (const uint8_t *)cmd + space + 1U, n - space - 1U);
}

void gs232b_extension_registry_init(gs232b_extension_registry_t *registry)
{
    if (registry != NULL) memset(registry, 0, sizeof(*registry));
}

gs232b_error_t gs232b_extension_register(gs232b_extension_registry_t *registry,
    const gs232b_extension_handler_t *handler)
{
    if (registry == NULL || handler == NULL || handler->domain == NULL ||
        handler->action == NULL || handler->handle == NULL ||
        !gs232b_json_schema_valid(handler->schema)) return GS232B_ERROR_INVALID_ARGUMENT;
    char domain[GS232B_EXTENSION_NAME_MAX + 1U], action[GS232B_EXTENSION_NAME_MAX + 1U];
    if (!name_copy(domain, (const uint8_t *)handler->domain, strlen(handler->domain)) ||
        !name_copy(action, (const uint8_t *)handler->action, strlen(handler->action))) return GS232B_ERROR_INVALID_ARGUMENT;
    gs232b_extension_entry_t entry = {0};
    size_t n = strlen(domain);
    memcpy(entry.cmd, domain, n);
    entry.cmd[n++] = ' ';
    memcpy(entry.cmd + n, action, strlen(action) + 1U);
    for (size_t i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].cmd, entry.cmd) == 0) return GS232B_ERROR_CONFLICT;
    }
    if (registry->count >= GS232B_EXTENSION_MAX_HANDLERS) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    entry.schema = handler->schema;
    entry.check = handler->check;
    entry.handle = handler->handle;
    entry.ctx = handler->ctx;
    registry->entries[registry->count++] = entry;
    return GS232B_OK;
}

gs232b_error_t gs232b_extension_parse(const uint8_t *line, size_t size,
    gs232b_extension_request_t *request)
{
    if (request == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    *request = (gs232b_extension_request_t){0};
    if (line == NULL || size == 0) return GS232B_ERROR_INVALID_ARGUMENT;
    if (size > GS232B_EXTENSION_LINE_MAX_BYTES) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    for (size_t j = 0; j < size; ++j) if (line[j] < 0x20 || line[j] == 0x7f) return GS232B_ERROR_INVALID_ARGUMENT;
    size_t i = 0;
    while (i < size && line[i] == ' ') ++i;
    if (i == size || line[i++] != '!') return GS232B_ERROR_INVALID_ARGUMENT;
    size_t start = i;
    while (i < size && name_byte(line[i])) ++i;
    char domain[GS232B_EXTENSION_NAME_MAX + 1U], action[GS232B_EXTENSION_NAME_MAX + 1U];
    if (!name_copy(domain, line + start, i - start) || i == size || line[i] != ' ') return GS232B_ERROR_INVALID_ARGUMENT;
    while (i < size && line[i] == ' ') ++i;
    start = i;
    while (i < size && name_byte(line[i])) ++i;
    if (!name_copy(action, line + start, i - start) || (i < size && line[i] != ' ')) return GS232B_ERROR_INVALID_ARGUMENT;
    size_t n = strlen(domain);
    memcpy(request->cmd, domain, n);
    request->cmd[n++] = ' ';
    memcpy(request->cmd + n, action, strlen(action) + 1U);
    while (i < size && line[i] == ' ') ++i;
    request->has_json = i < size;
    gs232b_byte_span_t raw = request->has_json ? (gs232b_byte_span_t){line + i, size - i} :
        (gs232b_byte_span_t){empty_object, sizeof(empty_object) - 1U};
    gs232b_error_t error = gs232b_json_parse(raw, &request->arguments);
    return error == GS232B_OK && request->arguments.type == GS232B_JSON_OBJECT ? GS232B_OK : GS232B_ERROR_INVALID_ARGUMENT;
}

static const gs232b_extension_entry_t *find(const gs232b_extension_registry_t *registry,
    const gs232b_extension_request_t *request)
{
    if (registry == NULL || request == NULL || !command_valid(request->cmd) || request->cmd[0] == '\0') return NULL;
    for (size_t i = 0; i < registry->count; ++i) if (strcmp(registry->entries[i].cmd, request->cmd) == 0) return &registry->entries[i];
    return NULL;
}

gs232b_error_t gs232b_extension_validate(const gs232b_extension_registry_t *registry,
    const gs232b_extension_request_t *request)
{
    const gs232b_extension_entry_t *entry = find(registry, request);
    if (entry == NULL) return GS232B_ERROR_NOT_FOUND;
    gs232b_error_t error = gs232b_json_validate_object(request->arguments, entry->schema);
    if (error == GS232B_OK && entry->check != NULL) error = entry->check(entry->ctx, request);
    return error;
}

void gs232b_extension_response_error(const char *cmd, gs232b_error_t error,
    gs232b_extension_response_t *response)
{
    if (response == NULL) return;
    if (error < GS232B_OK || error > GS232B_ERROR_CORRUPT) error = GS232B_ERROR_HARDWARE_FAULT;
    *response = (gs232b_extension_response_t){.result = error};
    if (cmd != NULL && command_valid(cmd)) memcpy(response->cmd, cmd, strlen(cmd) + 1U);
}

const char *gs232b_extension_error_name(gs232b_error_t error)
{
    return gs232b_error_name(error > GS232B_OK && error <= GS232B_ERROR_CORRUPT ?
        error : GS232B_ERROR_HARDWARE_FAULT);
}

gs232b_error_t gs232b_extension_dispatch(const gs232b_extension_registry_t *registry,
    const gs232b_extension_request_t *request, uint8_t *data, size_t capacity,
    gs232b_extension_response_t *response)
{
    if (response == NULL || request == NULL || (capacity != 0 && data == NULL)) return GS232B_ERROR_INVALID_ARGUMENT;
    gs232b_error_t error = gs232b_extension_validate(registry, request);
    size_t size = 0;
    if (error == GS232B_OK) {
        const gs232b_extension_entry_t *entry = find(registry, request);
        error = entry->handle(entry->ctx, request, data, capacity, &size);
        if (error == GS232B_OK && (size > capacity || size > GS232B_EXTENSION_DATA_MAX_BYTES)) error = GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    gs232b_extension_response_error(request->cmd, error, response);
    if (error == GS232B_OK) {
        gs232b_json_value_t value;
        gs232b_byte_span_t raw = size != 0 ? (gs232b_byte_span_t){data, size} :
            (gs232b_byte_span_t){empty_object, sizeof(empty_object) - 1U};
        if (gs232b_json_parse(raw, &value) != GS232B_OK || value.type != GS232B_JSON_OBJECT) error = GS232B_ERROR_CORRUPT;
        else response->data = value.raw;
        response->result = error;
    }
    return error;
}

typedef struct { uint8_t *bytes; size_t size; } writer_t;

static void append(writer_t *w, const void *bytes, size_t size)
{
    if (w->bytes != NULL && size != 0) memcpy(w->bytes + w->size, bytes, size);
    w->size += size;
}

static void text(writer_t *w, const char *value)
{
    append(w, value, strlen(value));
}

static void encode(writer_t *w, const gs232b_extension_response_t *response)
{
    text(w, response->result == GS232B_OK ? "!{\"ok\":true,\"cmd\":\"" : "!{\"ok\":false,\"cmd\":\"");
    text(w, response->cmd);
    if (response->result == GS232B_OK) {
        text(w, "\",\"data\":");
        if (response->data.size != 0) append(w, response->data.bytes, response->data.size);
        else text(w, "{}");
        text(w, "}\r\n");
    } else {
        text(w, "\",\"error\":\"");
        text(w, gs232b_extension_error_name(response->result));
        text(w, "\"}\r\n");
    }
}

gs232b_error_t gs232b_extension_response_encode(const gs232b_extension_response_t *response,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    if (size == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    *size = 0;
    if (response == NULL || (capacity != 0 && buffer == NULL) || !command_valid(response->cmd) ||
        response->result < GS232B_OK || response->result > GS232B_ERROR_CORRUPT) return GS232B_ERROR_INVALID_ARGUMENT;
    if (response->result == GS232B_OK && response->data.size != 0) {
        gs232b_json_value_t value;
        if (response->data.size > GS232B_EXTENSION_DATA_MAX_BYTES || gs232b_json_parse(response->data, &value) != GS232B_OK ||
            value.type != GS232B_JSON_OBJECT) return GS232B_ERROR_INVALID_ARGUMENT;
    }
    writer_t count = {0};
    encode(&count, response);
    *size = count.size;
    if (buffer == NULL || capacity < count.size) return GS232B_ERROR_BUFFER_TOO_SMALL;
    writer_t out = {buffer, 0};
    encode(&out, response);
    return GS232B_OK;
}
