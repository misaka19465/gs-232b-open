#include "gs232b/network_config.h"

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
            strcmp(active->wifi.password, candidate->wifi.password) != 0
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
