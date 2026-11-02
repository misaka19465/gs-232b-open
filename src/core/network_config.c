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

#include "gs232b/network_config.h"
#include "gs232b/persistence.h"

#include <string.h>

bool gs232b_net_config_valid(const gs232b_net_config_t *config)
{
    if (config == NULL || !gs232b_wifi_sta_config_valid(&config->wifi)) return false;
    gs232b_ethernet_config_t eth = config->eth;
    if (eth.mode == GS232B_ETHERNET_DHCP) eth.ipv4 = (gs232b_ethernet_ipv4_t){0};
    return gs232b_ethernet_config_valid(&eth);
}

gs232b_net_changes_t gs232b_net_config_changes(const gs232b_net_config_t *active,
    const gs232b_net_config_t *candidate)
{
    const gs232b_ethernet_ipv4_t *a = &active->eth.ipv4, *b = &candidate->eth.ipv4;
    return (gs232b_net_changes_t){
        .eth = active->eth.mode != candidate->eth.mode ||
            (candidate->eth.mode == GS232B_ETHERNET_STATIC &&
             (a->address != b->address || a->netmask != b->netmask || a->gateway != b->gateway ||
              a->dns[0] != b->dns[0] || a->dns[1] != b->dns[1])),
        .wifi = active->wifi.enabled != candidate->wifi.enabled ||
            strcmp(active->wifi.ssid, candidate->wifi.ssid) != 0 ||
            strcmp(active->wifi.password, candidate->wifi.password) != 0 ||
            active->wifi.mode != candidate->wifi.mode ||
            (candidate->wifi.mode == GS232B_WIFI_STA_STATIC &&
             (active->wifi.ipv4.address != candidate->wifi.ipv4.address ||
              active->wifi.ipv4.netmask != candidate->wifi.ipv4.netmask ||
              active->wifi.ipv4.gateway != candidate->wifi.ipv4.gateway))
    };
}

static void discard(gs232b_config_transaction_t *transaction)
{
    /* NOTE: volatile 擦除候选凭据，避免仅清 ID 后残留，且保留单调分配的 last_id。 */
    volatile unsigned char *bytes = (volatile unsigned char *)&transaction->candidate;
    for (size_t i = 0; i < sizeof(transaction->candidate); ++i) bytes[i] = 0;
    transaction->session = (gs232b_session_key_t){0};
    transaction->candidate_id = 0;
    transaction->base_revision = 0;
    transaction->renewed_us = 0;
}

static void clear_trial(gs232b_config_transaction_t *transaction)
{
    volatile unsigned char *bytes = (volatile unsigned char *)&transaction->trial.config;
    for (size_t i = 0; i < sizeof(transaction->trial.config); ++i) bytes[i] = 0;
    transaction->trial = (gs232b_config_trial_t){0};
}

static bool apply_accepted(gs232b_error_t error)
{
    return error == GS232B_OK || error == GS232B_ERROR_WOULD_BLOCK;
}

static gs232b_error_t restore_active(gs232b_config_transaction_t *transaction,
    gs232b_net_apply_port_t apply)
{
    if (apply.apply == NULL) return GS232B_ERROR_NOT_READY;
    const gs232b_error_t error = apply.apply(apply.ctx, &transaction->active);
    if (apply_accepted(error)) {
        clear_trial(transaction);
        return GS232B_OK;
    }
    transaction->trial.state = GS232B_CONFIG_TRIAL_ROLLBACK_PENDING;
    return error;
}

static gs232b_network_config_t persisted_config(const gs232b_net_config_t *config,
    uint32_t confirmed_trial_id)
{
    gs232b_network_config_t persisted = {
        .eth = config->eth,
        .enabled = config->wifi.enabled,
        .security = config->wifi.password[0] == '\0' ? GS232B_NETWORK_SECURITY_OPEN :
            GS232B_NETWORK_SECURITY_WPA2_PERSONAL,
        .wifi_mode = config->wifi.mode,
        .wifi_ipv4 = config->wifi.mode == GS232B_WIFI_STA_STATIC ? config->wifi.ipv4 :
            (gs232b_wifi_sta_address_t){0},
        .ssid_length = (uint8_t)strlen(config->wifi.ssid),
        .password_length = (uint8_t)strlen(config->wifi.password),
        .last_confirmed_trial_id = confirmed_trial_id
    };
    if (persisted.eth.mode == GS232B_ETHERNET_DHCP) {
        /* NOTE: DHCP 的候选可保留静态编辑字段，但 active 记录只写实际生效形状。 */
        persisted.eth.ipv4 = (gs232b_ethernet_ipv4_t){0};
    }
    memcpy(persisted.ssid, config->wifi.ssid, persisted.ssid_length + 1U);
    memcpy(persisted.password, config->wifi.password, persisted.password_length + 1U);
    return persisted;
}

gs232b_error_t gs232b_config_transaction_init(gs232b_config_transaction_t *transaction,
    gs232b_clock_t clock, const gs232b_net_config_t *active, uint64_t revision)
{
    const gs232b_net_config_t defaults = {0};
    if (active == NULL) active = &defaults;
    if (transaction == NULL || clock.now_us == NULL || !gs232b_net_config_valid(active)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    *transaction = (gs232b_config_transaction_t){
        .clock = clock, .active = *active, .active_revision = revision
    };
    return GS232B_OK;
}

bool gs232b_config_transaction_live(const gs232b_config_transaction_t *transaction)
{
    if (transaction == NULL || transaction->clock.now_us == NULL ||
        transaction->candidate_id == 0) return false;
    const gs232b_time_us_t now = transaction->clock.now_us(transaction->clock.ctx);
    return now >= transaction->renewed_us &&
        now - transaction->renewed_us < GS232B_CONFIG_IDLE_TIMEOUT_US;
}

void gs232b_config_transaction_expire(gs232b_config_transaction_t *transaction)
{
    if (transaction != NULL && transaction->candidate_id != 0 &&
        !gs232b_config_transaction_live(transaction)) discard(transaction);
}

void gs232b_config_transaction_close(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session)
{
    if (transaction != NULL && gs232b_session_key_equal(transaction->session, session)) {
        discard(transaction);
    }
}

gs232b_error_t gs232b_config_transaction_begin(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session)
{
    if (transaction == NULL || transaction->clock.now_us == NULL ||
        !gs232b_session_key_valid(session)) return GS232B_ERROR_INVALID_ARGUMENT;
    gs232b_config_transaction_expire(transaction);
    if (transaction->candidate_id != 0) return GS232B_ERROR_CONFLICT;
    if (transaction->last_id == UINT32_MAX) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    transaction->candidate = transaction->active;
    transaction->base_revision = transaction->active_revision;
    transaction->session = session;
    transaction->candidate_id = ++transaction->last_id;
    transaction->renewed_us = transaction->clock.now_us(transaction->clock.ctx);
    return GS232B_OK;
}

gs232b_error_t gs232b_config_transaction_access(const gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session)
{
    if (transaction == NULL || !gs232b_session_key_valid(session)) return GS232B_ERROR_INVALID_ARGUMENT;
    if (!gs232b_config_transaction_live(transaction)) return GS232B_ERROR_NOT_READY;
    if (!gs232b_session_key_equal(transaction->session, session) ||
        transaction->base_revision != transaction->active_revision) return GS232B_ERROR_CONFLICT;
    return GS232B_OK;
}

gs232b_error_t gs232b_config_transaction_update(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session, const gs232b_net_config_t *candidate)
{
    if (candidate == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    const gs232b_error_t error = gs232b_config_transaction_access(transaction, session);
    if (error != GS232B_OK) return error;
    transaction->candidate = *candidate;
    transaction->renewed_us = transaction->clock.now_us(transaction->clock.ctx);
    return GS232B_OK;
}

gs232b_error_t gs232b_config_transaction_check(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session, gs232b_net_changes_t *changes)
{
    if (changes == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    *changes = (gs232b_net_changes_t){0};
    const gs232b_error_t error = gs232b_config_transaction_access(transaction, session);
    if (error != GS232B_OK) return error;
    if (!gs232b_net_config_valid(&transaction->candidate)) return GS232B_ERROR_INVALID_ARGUMENT;
    *changes = gs232b_net_config_changes(&transaction->active, &transaction->candidate);
    transaction->renewed_us = transaction->clock.now_us(transaction->clock.ctx);
    return GS232B_OK;
}

gs232b_error_t gs232b_config_transaction_abort(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session)
{
    const gs232b_error_t error = gs232b_config_transaction_access(transaction, session);
    if (error == GS232B_OK) discard(transaction);
    return error;
}

gs232b_error_t gs232b_config_trial_apply(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session, uint32_t candidate_id, uint32_t timeout_s,
    const gs232b_store_t *store, gs232b_net_apply_port_t apply, uint32_t *trial_id)
{
    if (trial_id == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    *trial_id = 0;
    if (transaction == NULL || apply.apply == NULL ||
        timeout_s < GS232B_CONFIG_TRIAL_MIN_SECONDS ||
        timeout_s > GS232B_CONFIG_TRIAL_MAX_SECONDS) return GS232B_ERROR_INVALID_ARGUMENT;
    if (transaction->trial.state != GS232B_CONFIG_TRIAL_NONE) return GS232B_ERROR_BUSY;
    gs232b_error_t error = gs232b_config_transaction_access(transaction, session);
    if (error != GS232B_OK) return error;
    if (transaction->candidate_id != candidate_id ||
        transaction->base_revision != transaction->active_revision) return GS232B_ERROR_CONFLICT;
    if (!gs232b_net_config_valid(&transaction->candidate)) return GS232B_ERROR_INVALID_ARGUMENT;
    if (transaction->last_trial_id == UINT32_MAX) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    const gs232b_time_us_t now = transaction->clock.now_us(transaction->clock.ctx);
    const gs232b_time_us_t duration = (gs232b_time_us_t)timeout_s * UINT64_C(1000000);
    if (duration > UINT64_MAX - now) return GS232B_ERROR_RESOURCE_EXHAUSTED;
    const uint32_t next_id = transaction->last_trial_id + 1U;
    error = gs232b_persistence_trial_id_commit(store, transaction->last_trial_id, next_id);
    if (error != GS232B_OK) return error;
    transaction->last_trial_id = next_id;
    const gs232b_net_config_t candidate = transaction->candidate;
    const gs232b_error_t apply_error = apply.apply(apply.ctx, &candidate);
    if (!apply_accepted(apply_error)) {
        transaction->trial = (gs232b_config_trial_t){
            .state = GS232B_CONFIG_TRIAL_ROLLBACK_PENDING,
            .config = candidate,
            .id = next_id,
            .timeout_s = timeout_s,
            .base_revision = transaction->active_revision,
            .started_us = now,
            .deadline_us = now + duration
        };
        discard(transaction);
        const gs232b_error_t rollback_error = restore_active(transaction, apply);
        *trial_id = next_id;
        return rollback_error == GS232B_OK ? apply_error : rollback_error;
    }
    transaction->trial = (gs232b_config_trial_t){
        .state = GS232B_CONFIG_TRIAL_ACTIVE,
        .config = candidate,
        .id = next_id,
        .timeout_s = timeout_s,
        .base_revision = transaction->active_revision,
        .started_us = now,
        .deadline_us = now + duration
    };
    discard(transaction);
    *trial_id = next_id;
    return GS232B_OK;
}

gs232b_error_t gs232b_config_trial_confirm(gs232b_config_transaction_t *transaction,
    uint32_t trial_id, const gs232b_store_t *store, gs232b_net_apply_port_t apply)
{
    if (transaction == NULL || trial_id == 0) return GS232B_ERROR_INVALID_ARGUMENT;
    if (transaction->trial.state != GS232B_CONFIG_TRIAL_NONE &&
        transaction->trial.id != trial_id) return GS232B_ERROR_CONFLICT;
    if (trial_id == transaction->last_confirmed_trial_id) return GS232B_OK;
    if (transaction->trial.state == GS232B_CONFIG_TRIAL_ACTIVE) {
        const gs232b_time_us_t now = transaction->clock.now_us(transaction->clock.ctx);
        if (now < transaction->trial.started_us || now >= transaction->trial.deadline_us) {
            const gs232b_error_t rollback = restore_active(transaction, apply);
            return rollback == GS232B_OK ? GS232B_ERROR_TIMEOUT : rollback;
        }
    }
    if (transaction->trial.state != GS232B_CONFIG_TRIAL_ACTIVE ||
        transaction->trial.id != trial_id) return GS232B_ERROR_CONFLICT;
    if (transaction->trial.base_revision != transaction->active_revision) {
        (void)restore_active(transaction, apply);
        return GS232B_ERROR_CONFLICT;
    }
    const gs232b_network_config_t persisted = persisted_config(&transaction->trial.config,
        trial_id);
    uint64_t new_revision = 0;
    const gs232b_error_t error = gs232b_persistence_network_commit(store,
        transaction->active_revision, &persisted, &new_revision);
    if (error != GS232B_OK) {
        const gs232b_error_t rollback_error = restore_active(transaction, apply);
        return rollback_error == GS232B_OK ? error : rollback_error;
    }
    transaction->active = transaction->trial.config;
    transaction->active_revision = new_revision;
    transaction->last_confirmed_trial_id = trial_id;
    clear_trial(transaction);
    return GS232B_OK;
}

gs232b_error_t gs232b_config_trial_abort(gs232b_config_transaction_t *transaction,
    uint32_t trial_id, gs232b_net_apply_port_t apply)
{
    if (transaction == NULL || trial_id == 0) return GS232B_ERROR_INVALID_ARGUMENT;
    if (transaction->trial.state == GS232B_CONFIG_TRIAL_NONE ||
        transaction->trial.id != trial_id) return GS232B_ERROR_CONFLICT;
    return restore_active(transaction, apply);
}

gs232b_error_t gs232b_config_trial_tick(gs232b_config_transaction_t *transaction,
    gs232b_net_apply_port_t apply)
{
    if (transaction == NULL) return GS232B_ERROR_INVALID_ARGUMENT;
    if (transaction->trial.state == GS232B_CONFIG_TRIAL_NONE) return GS232B_OK;
    const gs232b_time_us_t now = transaction->clock.now_us(transaction->clock.ctx);
    if (transaction->trial.state == GS232B_CONFIG_TRIAL_ACTIVE &&
        now >= transaction->trial.started_us && now < transaction->trial.deadline_us) {
        return GS232B_OK;
    }
    return restore_active(transaction, apply);
}
