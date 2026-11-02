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

#include "gs232b/persistence.h"

#include <stdbool.h>
#include <string.h>

#define RECORD_MAGIC_0 UINT8_C(0x47)
#define RECORD_MAGIC_1 UINT8_C(0x53)
#define RECORD_MAGIC_2 UINT8_C(0x43)
#define RECORD_MAGIC_3 UINT8_C(0x46)
#define RECORD_FORMAT_VERSION GS232B_PERSISTENCE_RECORD_FORMAT_VERSION
#define RECORD_CRC_OFFSET GS232B_PERSISTENCE_RECORD_HEADER_SIZE

static bool domain_valid(gs232b_store_domain_t domain)
{
    return domain >= GS232B_STORE_NETWORK && domain < GS232B_STORE_DOMAIN_COUNT;
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= (uint64_t)bytes[i] << (i * 8U);
    }
    return value;
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & UINT16_C(0xff));
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8U));
    }
}

static void write_u64(uint8_t *bytes, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8U));
    }
}

static gs232b_error_t decode_record(gs232b_store_domain_t expected_domain,
                                    const uint8_t *bytes, size_t size,
                                    gs232b_persisted_record_t *record)
{
    if (size < GS232B_PERSISTENCE_RECORD_HEADER_SIZE +
                   GS232B_PERSISTENCE_RECORD_CHECKSUM_SIZE ||
        bytes[0] != RECORD_MAGIC_0 || bytes[1] != RECORD_MAGIC_1 ||
        bytes[2] != RECORD_MAGIC_2 || bytes[3] != RECORD_MAGIC_3 ||
        bytes[4] != RECORD_FORMAT_VERSION || bytes[5] != (uint8_t)expected_domain) {
        return GS232B_ERROR_CORRUPT;
    }

    const uint16_t payload_size = read_u16(&bytes[16]);
    const size_t expected_size = GS232B_PERSISTENCE_RECORD_HEADER_SIZE +
                                 (size_t)payload_size +
                                 GS232B_PERSISTENCE_RECORD_CHECKSUM_SIZE;
    if (payload_size > GS232B_PERSISTENCE_MAX_PAYLOAD_SIZE ||
        size != expected_size || read_u16(&bytes[6]) == 0 || read_u64(&bytes[8]) == 0 ||
        read_u32(&bytes[RECORD_CRC_OFFSET + payload_size]) !=
            crc32(bytes, RECORD_CRC_OFFSET + payload_size)) {
        return GS232B_ERROR_CORRUPT;
    }

    record->domain = expected_domain;
    record->schema_version = read_u16(&bytes[6]);
    record->revision = read_u64(&bytes[8]);
    record->payload_size = payload_size;
    if (payload_size != 0) {
        memcpy(record->payload, &bytes[RECORD_CRC_OFFSET], payload_size);
    }
    return GS232B_OK;
}

static size_t encode_record(gs232b_store_domain_t domain, uint16_t schema_version,
                            uint64_t revision, const uint8_t *payload,
                            uint16_t payload_size, uint8_t *bytes)
{
    bytes[0] = RECORD_MAGIC_0;
    bytes[1] = RECORD_MAGIC_1;
    bytes[2] = RECORD_MAGIC_2;
    bytes[3] = RECORD_MAGIC_3;
    bytes[4] = RECORD_FORMAT_VERSION;
    bytes[5] = (uint8_t)domain;
    write_u16(&bytes[6], schema_version);
    write_u64(&bytes[8], revision);
    write_u16(&bytes[16], payload_size);
    if (payload_size != 0) {
        memcpy(&bytes[RECORD_CRC_OFFSET], payload, payload_size);
    }
    const uint32_t checksum = crc32(bytes, RECORD_CRC_OFFSET + payload_size);
    write_u32(&bytes[RECORD_CRC_OFFSET + payload_size], checksum);
    return RECORD_CRC_OFFSET + payload_size + GS232B_PERSISTENCE_RECORD_CHECKSUM_SIZE;
}

gs232b_error_t gs232b_persistence_load_previous(const gs232b_store_t *store,
                                               gs232b_store_domain_t domain,
                                               gs232b_persisted_record_t *record)
{
    if (record == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(record, 0, sizeof(*record));
    uint8_t bytes[GS232B_PERSISTENCE_MAX_RECORD_SIZE];
    size_t size = 0;
    gs232b_error_t error = gs232b_store_load_previous(store, domain, bytes,
                                                       sizeof(bytes), &size);
    if (error != GS232B_OK) {
        return error;
    }
    return decode_record(domain, bytes, size, record);
}

gs232b_error_t gs232b_persistence_load(const gs232b_store_t *store,
                                      gs232b_store_domain_t domain,
                                      gs232b_persisted_record_t *record)
{
    if (record == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    memset(record, 0, sizeof(*record));
    if (store == NULL || !domain_valid(domain)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    uint8_t bytes[GS232B_PERSISTENCE_MAX_RECORD_SIZE];
    size_t size = 0;
    gs232b_error_t error = gs232b_store_load(store, domain, bytes, sizeof(bytes), &size);
    if (error != GS232B_OK) {
        if (error == GS232B_ERROR_NOT_FOUND || error == GS232B_ERROR_BUFFER_TOO_SMALL) {
            const gs232b_error_t previous_error =
                gs232b_persistence_load_previous(store, domain, record);
            if (previous_error == GS232B_OK) {
                return GS232B_OK;
            }
            if (previous_error != GS232B_ERROR_NOT_FOUND) {
                return previous_error;
            }
        }
        return error;
    }
    error = decode_record(domain, bytes, size, record);
    if (error == GS232B_ERROR_CORRUPT) {
        const gs232b_error_t previous_error =
            gs232b_persistence_load_previous(store, domain, record);
        if (previous_error != GS232B_ERROR_NOT_FOUND) {
            error = previous_error;
        }
    }
    if (error != GS232B_OK) {
        memset(record, 0, sizeof(*record));
    }
    return error;
}

gs232b_error_t gs232b_persistence_load_schema(const gs232b_store_t *store,
                                             gs232b_store_domain_t domain,
                                             uint16_t schema_version,
                                             gs232b_persisted_record_t *record)
{
    if (record == NULL || schema_version == 0) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_error_t error = gs232b_persistence_load(store, domain, record);
    if (error != GS232B_OK || record->schema_version == schema_version) {
        return error;
    }
    error = gs232b_persistence_load_previous(store, domain, record);
    if (error != GS232B_OK || record->schema_version != schema_version) {
        memset(record, 0, sizeof(*record));
        return error == GS232B_ERROR_NOT_FOUND ? GS232B_ERROR_CORRUPT :
               (error == GS232B_OK ? GS232B_ERROR_CORRUPT : error);
    }
    return GS232B_OK;
}

gs232b_error_t gs232b_persistence_commit(const gs232b_store_t *store,
                                         gs232b_store_domain_t domain,
                                         uint64_t expected_revision,
                                         uint16_t schema_version,
                                         const uint8_t *payload,
                                         size_t payload_size,
                                         uint64_t *new_revision)
{
    if (new_revision == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *new_revision = 0;
    if (store == NULL || !domain_valid(domain) || schema_version == 0 ||
        payload_size > GS232B_PERSISTENCE_MAX_PAYLOAD_SIZE ||
        (payload_size != 0 && payload == NULL)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }

    gs232b_persisted_record_t current;
    gs232b_error_t error = gs232b_persistence_load(store, domain, &current);
    uint64_t revision = 0;
    if (error == GS232B_OK) {
        revision = current.revision;
    } else if (error != GS232B_ERROR_NOT_FOUND) {
        return error;
    }
    if (revision != expected_revision) {
        return GS232B_ERROR_CONFLICT;
    }
    if (revision == UINT64_MAX) {
        return GS232B_ERROR_RESOURCE_EXHAUSTED;
    }

    uint8_t bytes[GS232B_PERSISTENCE_MAX_RECORD_SIZE];
    const uint64_t next_revision = revision + 1U;
    const size_t size = encode_record(domain, schema_version, next_revision, payload,
                                      (uint16_t)payload_size, bytes);
    error = gs232b_store_save(store, domain, bytes, size);
    if (error != GS232B_OK) {
        /* NOTE: a store may report an error after its atomic selector was committed. */
        gs232b_persisted_record_t uncertain;
        const gs232b_error_t readback = gs232b_persistence_load(store, domain, &uncertain);
        if (readback != GS232B_OK || uncertain.revision != next_revision ||
            uncertain.schema_version != schema_version || uncertain.payload_size != payload_size ||
            (payload_size != 0 && memcmp(uncertain.payload, payload, payload_size) != 0)) {
            return error;
        }
        *new_revision = next_revision;
        return GS232B_OK;
    }

    gs232b_persisted_record_t committed;
    error = gs232b_persistence_load(store, domain, &committed);
    if (error != GS232B_OK || committed.revision != next_revision ||
        committed.schema_version != schema_version ||
        committed.payload_size != payload_size ||
        (payload_size != 0 && memcmp(committed.payload, payload, payload_size) != 0)) {
        return error == GS232B_OK ? GS232B_ERROR_IO : error;
    }
    *new_revision = next_revision;
    return GS232B_OK;
}

gs232b_error_t gs232b_persistence_display_load(
    const gs232b_store_t *store,
    gs232b_azimuth_display_settings_t *settings,
    uint64_t *revision)
{
    if (settings == NULL || revision == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_azimuth_display_settings_init(settings);
    *revision = 0;

    gs232b_persisted_record_t record;
    gs232b_error_t error = gs232b_persistence_load_schema(
        store, GS232B_STORE_DISPLAY, GS232B_PERSISTENCE_SCHEMA_DISPLAY, &record);
    if (error != GS232B_OK) {
        return error;
    }
    const bool valid_payload = record.payload_size == 2 &&
        (record.payload[0] == GS232B_AZIMUTH_DISPLAY_P36 ||
         record.payload[0] == GS232B_AZIMUTH_DISPLAY_P45) &&
        (record.payload[1] == GS232B_AZIMUTH_CENTER_NORTH ||
         record.payload[1] == GS232B_AZIMUTH_CENTER_SOUTH);
    if (!valid_payload) {
        gs232b_persisted_record_t previous;
        error = gs232b_persistence_load_previous(store, GS232B_STORE_DISPLAY, &previous);
        if (error != GS232B_OK || previous.schema_version !=
                GS232B_PERSISTENCE_SCHEMA_DISPLAY || previous.payload_size != 2 ||
            (previous.payload[0] != GS232B_AZIMUTH_DISPLAY_P36 &&
             previous.payload[0] != GS232B_AZIMUTH_DISPLAY_P45) ||
            (previous.payload[1] != GS232B_AZIMUTH_CENTER_NORTH &&
             previous.payload[1] != GS232B_AZIMUTH_CENTER_SOUTH)) {
            return GS232B_ERROR_CORRUPT;
        }
        record = previous;
    }
    settings->mode = (gs232b_azimuth_display_mode_t)record.payload[0];
    settings->center = (gs232b_azimuth_center_t)record.payload[1];
    *revision = record.revision;
    return GS232B_OK;
}

gs232b_error_t gs232b_persistence_display_commit(
    const gs232b_store_t *store,
    uint64_t expected_revision,
    const gs232b_azimuth_display_settings_t *settings,
    uint64_t *new_revision)
{
    if (settings == NULL || new_revision == NULL ||
        (settings->mode != GS232B_AZIMUTH_DISPLAY_P36 &&
         settings->mode != GS232B_AZIMUTH_DISPLAY_P45) ||
        (settings->center != GS232B_AZIMUTH_CENTER_NORTH &&
         settings->center != GS232B_AZIMUTH_CENTER_SOUTH)) {
        if (new_revision != NULL) {
            *new_revision = 0;
        }
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t payload[] = {(uint8_t)settings->mode, (uint8_t)settings->center};
    return gs232b_persistence_commit(store, GS232B_STORE_DISPLAY, expected_revision,
                                     GS232B_PERSISTENCE_SCHEMA_DISPLAY, payload,
                                     sizeof(payload), new_revision);
}

static bool network_config_valid(const gs232b_network_config_t *config)
{
    if (config == NULL || !gs232b_ethernet_config_valid(&config->eth) ||
        config->ssid_length > GS232B_NETWORK_SSID_MAX ||
        config->password_length > GS232B_NETWORK_PASSWORD_MAX ||
        config->security > GS232B_NETWORK_SECURITY_WPA2_PERSONAL ||
        config->ssid[config->ssid_length] != '\0' ||
        config->password[config->password_length] != '\0') {
        return false;
    }
    if (config->wifi_mode != GS232B_WIFI_STA_DHCP && config->wifi_mode != GS232B_WIFI_STA_STATIC) return false;
    if (config->wifi_mode == GS232B_WIFI_STA_STATIC) {
        const gs232b_ethernet_config_t ip = {.mode = GS232B_ETHERNET_STATIC,
            .ipv4 = {.address = config->wifi_ipv4.address, .netmask = config->wifi_ipv4.netmask,
                .gateway = config->wifi_ipv4.gateway}};
        if (!gs232b_ethernet_config_valid(&ip)) return false;
    }
    if (!config->enabled && config->ssid_length == 0 && config->password_length == 0) {
        return true;
    }
    if (config->ssid_length == 0) {
        return false;
    }
    if (config->security == GS232B_NETWORK_SECURITY_OPEN) {
        return config->password_length == 0;
    }
    return config->password_length >= 8U;
}

static bool decode_network_payload(const gs232b_persisted_record_t *record,
                                   gs232b_network_config_t *config)
{
    const size_t legacy_size = 1U + 1U + GS232B_NETWORK_SSID_MAX + 1U +
                               GS232B_NETWORK_PASSWORD_MAX + 1U;
    const size_t current_size = 1U + 5U * sizeof(uint32_t) + legacy_size + sizeof(uint32_t);
    memset(config, 0, sizeof(*config));
    if (record->schema_version == 1U && record->payload_size == legacy_size) {
        const uint8_t *payload = record->payload;
        if (payload[0] > 1U) return false;
        config->enabled = payload[0] != 0;
        config->security = (gs232b_network_security_t)payload[1];
        config->ssid_length = payload[2];
        memcpy(config->ssid, &payload[3], GS232B_NETWORK_SSID_MAX);
        config->password_length = payload[3U + GS232B_NETWORK_SSID_MAX];
        memcpy(config->password, &payload[4U + GS232B_NETWORK_SSID_MAX],
               GS232B_NETWORK_PASSWORD_MAX);
        config->ssid[GS232B_NETWORK_SSID_MAX] = '\0';
        config->password[GS232B_NETWORK_PASSWORD_MAX] = '\0';
        return network_config_valid(config);
    }
    /* NOTE: schema 1/2 继续按原布局读取并默认 STA DHCP；仅新写入使用 schema 3。 */
    if ((record->schema_version == 2U && record->payload_size == current_size) ||
        (record->schema_version == GS232B_PERSISTENCE_SCHEMA_NETWORK &&
         record->payload_size == current_size + 13U)) {
        const uint8_t *payload = record->payload;
        config->eth.mode = (gs232b_ethernet_mode_t)payload[0];
        config->eth.ipv4.address = read_u32(&payload[1]);
        config->eth.ipv4.netmask = read_u32(&payload[5]);
        config->eth.ipv4.gateway = read_u32(&payload[9]);
        config->eth.ipv4.dns[0] = read_u32(&payload[13]);
        config->eth.ipv4.dns[1] = read_u32(&payload[17]);
        const uint8_t *wifi = &payload[21];
        config->enabled = wifi[0] != 0;
        config->security = (gs232b_network_security_t)wifi[1];
        config->ssid_length = wifi[2];
        memcpy(config->ssid, &wifi[3], GS232B_NETWORK_SSID_MAX);
        config->password_length = wifi[3U + GS232B_NETWORK_SSID_MAX];
        memcpy(config->password, &wifi[4U + GS232B_NETWORK_SSID_MAX],
               GS232B_NETWORK_PASSWORD_MAX);
        config->last_confirmed_trial_id = read_u32(&payload[21U + legacy_size]);
        if (record->schema_version == GS232B_PERSISTENCE_SCHEMA_NETWORK) {
            const uint8_t *ip = &payload[current_size];
            config->wifi_mode = (gs232b_wifi_sta_mode_t)ip[0];
            config->wifi_ipv4.address = read_u32(&ip[1]);
            config->wifi_ipv4.netmask = read_u32(&ip[5]);
            config->wifi_ipv4.gateway = read_u32(&ip[9]);
        }
        config->ssid[GS232B_NETWORK_SSID_MAX] = '\0';
        config->password[GS232B_NETWORK_PASSWORD_MAX] = '\0';
        return wifi[0] <= 1U && network_config_valid(config);
    }
    return false;
}

gs232b_error_t gs232b_persistence_network_load(
    const gs232b_store_t *store,
    gs232b_network_config_t *config,
    uint64_t *revision)
{
    if (config == NULL || revision == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    memset(config, 0, sizeof(*config));
    *revision = 0;
    gs232b_persisted_record_t record;
    gs232b_error_t error = gs232b_persistence_load(store, GS232B_STORE_NETWORK, &record);
    if (error != GS232B_OK) return error;
    if (!decode_network_payload(&record, config)) {
        error = gs232b_persistence_load_previous(store, GS232B_STORE_NETWORK, &record);
        if (error != GS232B_OK || !decode_network_payload(&record, config)) {
            memset(config, 0, sizeof(*config));
            return error != GS232B_OK && error != GS232B_ERROR_NOT_FOUND ? error :
                GS232B_ERROR_CORRUPT;
        }
    }
    *revision = record.revision;
    return GS232B_OK;
}

gs232b_error_t gs232b_persistence_network_commit(
    const gs232b_store_t *store,
    uint64_t expected_revision,
    const gs232b_network_config_t *config,
    uint64_t *new_revision)
{
    if (new_revision == NULL) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *new_revision = 0;
    if (!network_config_valid(config)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    const size_t legacy_size = 1U + 1U + GS232B_NETWORK_SSID_MAX + 1U +
                               GS232B_NETWORK_PASSWORD_MAX + 1U;
    const size_t base_size = 1U + 5U * sizeof(uint32_t) + legacy_size + sizeof(uint32_t);
    const size_t payload_size = base_size + 13U;
    uint8_t payload[1U + 5U * sizeof(uint32_t) + 1U + 1U + GS232B_NETWORK_SSID_MAX +
                    1U + GS232B_NETWORK_PASSWORD_MAX + 1U + sizeof(uint32_t) + 13U] = {0};
    payload[0] = (uint8_t)config->eth.mode;
    write_u32(&payload[1], config->eth.ipv4.address);
    write_u32(&payload[5], config->eth.ipv4.netmask);
    write_u32(&payload[9], config->eth.ipv4.gateway);
    write_u32(&payload[13], config->eth.ipv4.dns[0]);
    write_u32(&payload[17], config->eth.ipv4.dns[1]);
    uint8_t *wifi = &payload[21];
    wifi[0] = config->enabled ? 1U : 0U;
    wifi[1] = (uint8_t)config->security;
    wifi[2] = config->ssid_length;
    memcpy(&wifi[3], config->ssid, config->ssid_length);
    wifi[3U + GS232B_NETWORK_SSID_MAX] = config->password_length;
    memcpy(&wifi[4U + GS232B_NETWORK_SSID_MAX], config->password, config->password_length);
    write_u32(&payload[21U + legacy_size], config->last_confirmed_trial_id);
    payload[base_size] = (uint8_t)config->wifi_mode;
    if (config->wifi_mode == GS232B_WIFI_STA_STATIC) {
        write_u32(&payload[base_size + 1U], config->wifi_ipv4.address);
        write_u32(&payload[base_size + 5U], config->wifi_ipv4.netmask);
        write_u32(&payload[base_size + 9U], config->wifi_ipv4.gateway);
    }
    return gs232b_persistence_commit(store, GS232B_STORE_NETWORK, expected_revision,
                                     GS232B_PERSISTENCE_SCHEMA_NETWORK, payload,
                                     payload_size, new_revision);
}

gs232b_error_t gs232b_persistence_trial_id_load(const gs232b_store_t *store,
    uint32_t *last_trial_id)
{
    if (last_trial_id == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    *last_trial_id = 0;
    gs232b_persisted_record_t record;
    const gs232b_error_t error = gs232b_persistence_load_schema(store,
        GS232B_STORE_CONFIG_META, GS232B_PERSISTENCE_SCHEMA_CONFIG_META, &record);
    if (error == GS232B_ERROR_NOT_FOUND) return GS232B_OK;
    if (error != GS232B_OK) return error;
    if (record.payload_size != sizeof(uint32_t)) return GS232B_ERROR_CORRUPT;
    *last_trial_id = read_u32(record.payload);
    return GS232B_OK;
}

gs232b_error_t gs232b_persistence_trial_id_commit(const gs232b_store_t *store,
    uint32_t expected_last_trial_id, uint32_t next_trial_id)
{
    if (next_trial_id == 0 || expected_last_trial_id == UINT32_MAX ||
        next_trial_id != expected_last_trial_id + 1U) return GS232B_ERROR_INVALID_ARGUMENT;
    uint32_t current_id = 0;
    gs232b_error_t error = gs232b_persistence_trial_id_load(store, &current_id);
    if (error != GS232B_OK) return error;
    if (current_id != expected_last_trial_id) return GS232B_ERROR_CONFLICT;
    gs232b_persisted_record_t record;
    error = gs232b_persistence_load_schema(store, GS232B_STORE_CONFIG_META,
        GS232B_PERSISTENCE_SCHEMA_CONFIG_META, &record);
    uint64_t record_revision = 0;
    if (error == GS232B_OK) record_revision = record.revision;
    else if (error != GS232B_ERROR_NOT_FOUND) return error;
    uint8_t payload[sizeof(uint32_t)];
    write_u32(payload, next_trial_id);
    uint64_t new_revision;
    return gs232b_persistence_commit(store, GS232B_STORE_CONFIG_META, record_revision,
        GS232B_PERSISTENCE_SCHEMA_CONFIG_META, payload, sizeof(payload), &new_revision);
}
