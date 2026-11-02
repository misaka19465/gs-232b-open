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

#include "gs232b/extension_json.h"

#include <string.h>

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t pos;
} cursor_t;

static void spaces(cursor_t *c)
{
    while (c->pos < c->size && (c->bytes[c->pos] == ' ' || c->bytes[c->pos] == '\t')) ++c->pos;
}

static bool hex4(cursor_t *c, uint32_t *value)
{
    if (c->size - c->pos < 4) return false;
    *value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t b = c->bytes[c->pos++];
        unsigned digit;
        if (b >= '0' && b <= '9') digit = b - '0';
        else if (b >= 'a' && b <= 'f') digit = b - 'a' + 10U;
        else if (b >= 'A' && b <= 'F') digit = b - 'A' + 10U;
        else return false;
        *value = *value * 16U + digit;
    }
    return true;
}

static bool utf8(cursor_t *c, uint32_t *cp)
{
    if (c->pos == c->size) return false;
    uint8_t first = c->bytes[c->pos++];
    unsigned count;
    uint32_t minimum;
    if (first < 0x80) { *cp = first; return true; }
    if (first >= 0xc2 && first <= 0xdf) { count = 1; *cp = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; *cp = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; *cp = first & 7; minimum = 0x10000; }
    else return false;
    if (c->size - c->pos < count) return false;
    for (unsigned i = 0; i < count; ++i) {
        uint8_t b = c->bytes[c->pos++];
        if ((b & 0xc0) != 0x80) return false;
        *cp = (*cp << 6) | (b & 0x3f);
    }
    return *cp >= minimum && *cp <= 0x10ffff && (*cp < 0xd800 || *cp > 0xdfff);
}

static size_t put_utf8(uint32_t cp, uint8_t out[4])
{
    if (cp < 0x80) { out[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xc0 | (cp >> 6)); out[1] = (uint8_t)(0x80 | (cp & 63)); return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xe0 | (cp >> 12)); out[1] = (uint8_t)(0x80 | ((cp >> 6) & 63));
        out[2] = (uint8_t)(0x80 | (cp & 63)); return 3;
    }
    out[0] = (uint8_t)(0xf0 | (cp >> 18)); out[1] = (uint8_t)(0x80 | ((cp >> 12) & 63));
    out[2] = (uint8_t)(0x80 | ((cp >> 6) & 63)); out[3] = (uint8_t)(0x80 | (cp & 63)); return 4;
}

static bool string_cp(cursor_t *c, uint32_t *cp)
{
    if (c->pos == c->size || c->bytes[c->pos] < 0x20 || c->bytes[c->pos] == '"') return false;
    if (c->bytes[c->pos] != '\\') return utf8(c, cp);
    if (++c->pos == c->size) return false;
    uint8_t b = c->bytes[c->pos++];
    switch (b) {
    case '"': case '\\': case '/': *cp = b; return true;
    case 'b': *cp = 8; return true;
    case 'f': *cp = 12; return true;
    case 'n': *cp = 10; return true;
    case 'r': *cp = 13; return true;
    case 't': *cp = 9; return true;
    case 'u': break;
    default: return false;
    }
    if (!hex4(c, cp) || (*cp >= 0xdc00 && *cp <= 0xdfff)) return false;
    if (*cp >= 0xd800 && *cp <= 0xdbff) {
        if (c->size - c->pos < 6 || c->bytes[c->pos] != '\\' || c->bytes[c->pos + 1] != 'u') return false;
        c->pos += 2;
        uint32_t low;
        if (!hex4(c, &low) || low < 0xdc00 || low > 0xdfff) return false;
        *cp = 0x10000 + ((*cp - 0xd800) << 10) + (low - 0xdc00);
    }
    return true;
}

static bool string(cursor_t *c, gs232b_byte_span_t *raw)
{
    size_t start = c->pos;
    if (c->pos == c->size || c->bytes[c->pos++] != '"') return false;
    while (c->pos < c->size && c->bytes[c->pos] != '"') {
        uint32_t cp;
        if (!string_cp(c, &cp)) return false;
    }
    if (c->pos == c->size) return false;
    ++c->pos;
    if (raw != NULL) *raw = (gs232b_byte_span_t){c->bytes + start, c->pos - start};
    return true;
}

static bool strings_equal(gs232b_byte_span_t a, gs232b_byte_span_t b)
{
    cursor_t ca = {a.bytes, a.size - 1U, 1}, cb = {b.bytes, b.size - 1U, 1};
    while (ca.pos < ca.size && cb.pos < cb.size) {
        uint32_t x, y;
        if (!string_cp(&ca, &x) || !string_cp(&cb, &y) || x != y) return false;
    }
    return ca.pos == ca.size && cb.pos == cb.size;
}

static bool key_equal(gs232b_byte_span_t raw, const char *name)
{
    cursor_t key = {raw.bytes, raw.size - 1U, 1};
    cursor_t plain = {(const uint8_t *)name, strlen(name), 0};
    while (key.pos < key.size && plain.pos < plain.size) {
        uint32_t x, y;
        if (!string_cp(&key, &x) || !utf8(&plain, &y) || x != y) return false;
    }
    return key.pos == key.size && plain.pos == plain.size;
}

static bool scan(cursor_t *c, unsigned depth, bool unique, gs232b_json_value_t *result);

static bool repeated_key(cursor_t previous, size_t end, gs232b_byte_span_t key, unsigned depth)
{
    previous.size = end;
    while (previous.pos < previous.size) {
        gs232b_byte_span_t other;
        if (!string(&previous, &other)) return true;
        if (strings_equal(key, other)) return true;
        spaces(&previous);
        if (previous.pos == previous.size || previous.bytes[previous.pos++] != ':') return true;
        if (!scan(&previous, depth, false, NULL)) return true;
        spaces(&previous);
        if (previous.pos < previous.size && previous.bytes[previous.pos] == ',') ++previous.pos;
        spaces(&previous);
    }
    return false;
}

static bool digits(cursor_t *c)
{
    size_t start = c->pos;
    while (c->pos < c->size && c->bytes[c->pos] >= '0' && c->bytes[c->pos] <= '9') ++c->pos;
    return c->pos > start;
}

static bool number(cursor_t *c)
{
    if (c->pos < c->size && c->bytes[c->pos] == '-') ++c->pos;
    if (c->pos == c->size) return false;
    if (c->bytes[c->pos] == '0') ++c->pos;
    else if (!digits(c)) return false;
    if (c->pos < c->size && c->bytes[c->pos] == '.') {
        ++c->pos;
        if (!digits(c)) return false;
    }
    if (c->pos < c->size && (c->bytes[c->pos] == 'e' || c->bytes[c->pos] == 'E')) {
        ++c->pos;
        if (c->pos < c->size && (c->bytes[c->pos] == '+' || c->bytes[c->pos] == '-')) ++c->pos;
        if (!digits(c)) return false;
    }
    return true;
}

static bool literal(cursor_t *c, const char *text)
{
    size_t n = strlen(text);
    if (c->size - c->pos < n || memcmp(c->bytes + c->pos, text, n) != 0) return false;
    c->pos += n;
    return true;
}

static bool scan(cursor_t *c, unsigned depth, bool unique, gs232b_json_value_t *result)
{
    spaces(c);
    if (c->pos == c->size || depth > GS232B_JSON_MAX_DEPTH) return false;
    size_t start = c->pos;
    uint8_t b = c->bytes[c->pos];
    gs232b_json_type_t type;
    if (b == '{' || b == '[') {
        type = b == '{' ? GS232B_JSON_OBJECT : GS232B_JSON_ARRAY;
        uint8_t close = b == '{' ? '}' : ']';
        ++c->pos; spaces(c);
        cursor_t members = *c;
        if (c->pos < c->size && c->bytes[c->pos] == close) ++c->pos;
        else for (;;) {
            if (b == '{') {
                size_t key_start = c->pos;
                gs232b_byte_span_t key;
                if (!string(c, &key) || (unique && repeated_key(members, key_start, key, depth + 1))) return false;
                spaces(c);
                if (c->pos == c->size || c->bytes[c->pos++] != ':') return false;
            }
            if (!scan(c, depth + 1, unique, NULL)) return false;
            spaces(c);
            if (c->pos == c->size) return false;
            b = type == GS232B_JSON_OBJECT ? '{' : '[';
            uint8_t separator = c->bytes[c->pos++];
            if (separator == close) break;
            if (separator != ',') return false;
            spaces(c);
        }
    } else if (b == '"') {
        type = GS232B_JSON_STRING;
        if (!string(c, NULL)) return false;
    } else if (b == 't' || b == 'f') {
        type = GS232B_JSON_BOOL;
        if (!literal(c, b == 't' ? "true" : "false")) return false;
    } else if (b == 'n') {
        type = GS232B_JSON_NULL;
        if (!literal(c, "null")) return false;
    } else {
        type = GS232B_JSON_NUMBER;
        if ((b != '-' && (b < '0' || b > '9')) || !number(c)) return false;
    }
    if (result != NULL) *result = (gs232b_json_value_t){type, {c->bytes + start, c->pos - start}};
    return true;
}

gs232b_error_t gs232b_json_parse(gs232b_byte_span_t raw, gs232b_json_value_t *value)
{
    if (raw.bytes == NULL || raw.size == 0 || value == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < raw.size; ++i) {
        if ((raw.bytes[i] < 0x20 && raw.bytes[i] != '\t') || raw.bytes[i] == 0x7f) return GS232B_ERROR_INVALID_ARGUMENT;
    }
    cursor_t c = {raw.bytes, raw.size, 0};
    if (!scan(&c, 0, true, value)) return GS232B_ERROR_INVALID_ARGUMENT;
    spaces(&c);
    return c.pos == c.size ? GS232B_OK : GS232B_ERROR_INVALID_ARGUMENT;
}

gs232b_error_t gs232b_json_string_decode(gs232b_json_value_t value,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    if (size == NULL || (capacity != 0 && buffer == NULL)) return GS232B_ERROR_INVALID_ARGUMENT;
    *size = 0;
    gs232b_json_value_t parsed;
    if (value.type != GS232B_JSON_STRING || gs232b_json_parse(value.raw, &parsed) != GS232B_OK || parsed.type != GS232B_JSON_STRING) return GS232B_ERROR_INVALID_ARGUMENT;
    for (unsigned pass = 0; pass < 2; ++pass) {
        cursor_t c = {parsed.raw.bytes, parsed.raw.size - 1U, 1};
        size_t n = 0;
        while (c.pos < c.size) {
            uint32_t cp; uint8_t bytes[4];
            if (!string_cp(&c, &cp)) return GS232B_ERROR_INVALID_ARGUMENT;
            size_t count = put_utf8(cp, bytes);
            if (pass != 0) memcpy(buffer + n, bytes, count);
            n += count;
        }
        *size = n;
        if (pass == 0 && (buffer == NULL || capacity < n)) return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_json_string_encode(gs232b_byte_span_t text,
    uint8_t *buffer, size_t capacity, size_t *size)
{
    if (size == NULL || (text.size != 0 && text.bytes == NULL) || (capacity != 0 && buffer == NULL)) return GS232B_ERROR_INVALID_ARGUMENT;
    *size = 0;
    static const char hex[] = "0123456789abcdef";
    for (unsigned pass = 0; pass < 2; ++pass) {
        cursor_t c = {text.bytes, text.size, 0};
        size_t n = 1;
        if (pass != 0) buffer[0] = '"';
        while (c.pos < c.size) {
            uint32_t cp; uint8_t bytes[6]; size_t count;
            if (!utf8(&c, &cp)) return GS232B_ERROR_INVALID_ARGUMENT;
            if (cp == '"' || cp == '\\') { bytes[0] = '\\'; bytes[1] = (uint8_t)cp; count = 2; }
            else if (cp < 0x20 || cp == 0x7f) {
                bytes[0] = '\\'; bytes[1] = 'u'; bytes[2] = '0'; bytes[3] = '0';
                bytes[4] = (uint8_t)hex[cp >> 4]; bytes[5] = (uint8_t)hex[cp & 15]; count = 6;
            } else count = put_utf8(cp, bytes);
            if (count > SIZE_MAX - n - 1U) return GS232B_ERROR_BUFFER_TOO_SMALL;
            if (pass != 0) memcpy(buffer + n, bytes, count);
            n += count;
        }
        if (pass != 0) buffer[n] = '"';
        *size = n + 1U;
        if (pass == 0 && (buffer == NULL || capacity < *size)) return GS232B_ERROR_BUFFER_TOO_SMALL;
    }
    return GS232B_OK;
}

static bool schema_valid(const gs232b_json_schema_t *schema, unsigned depth)
{
    if (schema == NULL) return true;
    if (depth > GS232B_JSON_MAX_DEPTH || schema->count > GS232B_JSON_MAX_FIELDS || (schema->count != 0 && schema->fields == NULL)) return false;
    for (size_t i = 0; i < schema->count; ++i) {
        const gs232b_json_field_t *field = &schema->fields[i];
        if (field->name == NULL || field->type < GS232B_JSON_OBJECT || field->type > GS232B_JSON_NULL ||
            (field->object != NULL && field->type != GS232B_JSON_OBJECT)) return false;
        cursor_t name = {(const uint8_t *)field->name, strlen(field->name), 0};
        while (name.pos < name.size) { uint32_t cp; if (!utf8(&name, &cp)) return false; }
        for (size_t j = 0; j < i; ++j) if (strcmp(field->name, schema->fields[j].name) == 0) return false;
        if (!schema_valid(field->object, depth + 1)) return false;
    }
    return true;
}

bool gs232b_json_schema_valid(const gs232b_json_schema_t *schema)
{
    return schema_valid(schema, 0);
}

static gs232b_error_t validate_object(gs232b_json_value_t object,
    const gs232b_json_schema_t *schema, unsigned depth)
{
    if (depth > GS232B_JSON_MAX_DEPTH || object.type != GS232B_JSON_OBJECT) return GS232B_ERROR_INVALID_ARGUMENT;
    cursor_t c = {object.raw.bytes, object.raw.size - 1U, 1};
    uint32_t seen = 0;
    spaces(&c);
    while (c.pos < c.size) {
        gs232b_byte_span_t key;
        if (!string(&c, &key)) return GS232B_ERROR_INVALID_ARGUMENT;
        size_t index = 0;
        while (schema != NULL && index < schema->count && !key_equal(key, schema->fields[index].name)) ++index;
        if (schema == NULL || index == schema->count || (seen & (UINT32_C(1) << index)) != 0) return GS232B_ERROR_INVALID_ARGUMENT;
        seen |= UINT32_C(1) << index;
        spaces(&c);
        if (c.pos == c.size || c.bytes[c.pos++] != ':') return GS232B_ERROR_INVALID_ARGUMENT;
        gs232b_json_value_t field;
        if (!scan(&c, 0, false, &field)) return GS232B_ERROR_INVALID_ARGUMENT;
        const gs232b_json_field_t *spec = &schema->fields[index];
        if (field.type != spec->type) return GS232B_ERROR_INVALID_ARGUMENT;
        if (field.type == GS232B_JSON_STRING && spec->max_bytes != 0) {
            size_t bytes;
            gs232b_error_t error = gs232b_json_string_decode(field, NULL, 0, &bytes);
            if (error != GS232B_ERROR_BUFFER_TOO_SMALL || bytes > spec->max_bytes) return GS232B_ERROR_INVALID_ARGUMENT;
        }
        if (field.type == GS232B_JSON_OBJECT && validate_object(field, spec->object, depth + 1) != GS232B_OK) return GS232B_ERROR_INVALID_ARGUMENT;
        spaces(&c);
        if (c.pos < c.size) { if (c.bytes[c.pos++] != ',') return GS232B_ERROR_INVALID_ARGUMENT; spaces(&c); }
    }
    if (schema != NULL) for (size_t i = 0; i < schema->count; ++i) {
        if (schema->fields[i].required && (seen & (UINT32_C(1) << i)) == 0) return GS232B_ERROR_INVALID_ARGUMENT;
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_json_validate_object(gs232b_json_value_t object,
    const gs232b_json_schema_t *schema)
{
    gs232b_json_value_t parsed;
    if (!gs232b_json_schema_valid(schema) || gs232b_json_parse(object.raw, &parsed) != GS232B_OK ||
        parsed.type != GS232B_JSON_OBJECT || object.type != GS232B_JSON_OBJECT) return GS232B_ERROR_INVALID_ARGUMENT;
    return validate_object(parsed, schema, 0);
}

gs232b_error_t gs232b_json_get(gs232b_json_value_t object, const char *name,
    gs232b_json_value_t *value)
{
    gs232b_json_value_t parsed;
    if (name == NULL || value == NULL || object.type != GS232B_JSON_OBJECT ||
        gs232b_json_parse(object.raw, &parsed) != GS232B_OK || parsed.type != GS232B_JSON_OBJECT) return GS232B_ERROR_INVALID_ARGUMENT;
    cursor_t c = {parsed.raw.bytes, parsed.raw.size - 1U, 1};
    spaces(&c);
    while (c.pos < c.size) {
        gs232b_byte_span_t key;
        if (!string(&c, &key)) return GS232B_ERROR_INVALID_ARGUMENT;
        spaces(&c); ++c.pos;
        if (!scan(&c, 0, false, value)) return GS232B_ERROR_INVALID_ARGUMENT;
        if (key_equal(key, name)) return GS232B_OK;
        spaces(&c);
        if (c.pos < c.size) ++c.pos;
        spaces(&c);
    }
    return GS232B_ERROR_NOT_FOUND;
}

gs232b_error_t gs232b_json_uint32(gs232b_json_value_t value, uint32_t *number)
{
    if (number == NULL || value.type != GS232B_JSON_NUMBER || value.raw.bytes == NULL || value.raw.size == 0) return GS232B_ERROR_INVALID_ARGUMENT;
    uint32_t n = 0;
    for (size_t i = 0; i < value.raw.size; ++i) {
        uint8_t b = value.raw.bytes[i];
        if (b < '0' || b > '9' || (i != 0 && value.raw.bytes[0] == '0') || n > (UINT32_MAX - (b - '0')) / 10U) return GS232B_ERROR_INVALID_ARGUMENT;
        n = n * 10U + (b - '0');
    }
    *number = n;
    return GS232B_OK;
}

gs232b_error_t gs232b_json_uint64(gs232b_json_value_t value, uint64_t *number)
{
    if (number == NULL || value.type != GS232B_JSON_NUMBER || value.raw.bytes == NULL ||
        value.raw.size == 0) return GS232B_ERROR_INVALID_ARGUMENT;
    uint64_t n = 0;
    for (size_t i = 0; i < value.raw.size; ++i) {
        const uint8_t digit = value.raw.bytes[i];
        if (digit < '0' || digit > '9' || (i != 0 && value.raw.bytes[0] == '0') ||
            n > (UINT64_MAX - (digit - '0')) / 10U) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
        n = n * 10U + (digit - '0');
    }
    *number = n;
    return GS232B_OK;
}
