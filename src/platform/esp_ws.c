#include "esp_ws_internal.h"

#include <string.h>
#include "esp_timer.h"

#if !CONFIG_HTTPD_WS_SUPPORT || !CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT || \
    !CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT || CONFIG_HTTPD_QUEUE_WORK_BLOCKING
#error "GS WebSocket requires WS pre/post hooks and nonblocking HTTPD work queue"
#endif

static gs232b_error_t sdk_error(esp_err_t error)
{
    if (error == ESP_OK) { return GS232B_OK; }
    if (error == ESP_ERR_NO_MEM) { return GS232B_ERROR_RESOURCE_EXHAUSTED; }
    if (error == ESP_ERR_INVALID_ARG) { return GS232B_ERROR_INVALID_ARGUMENT; }
    return GS232B_ERROR_IO;
}

static gs232b_ws_client_t *find(gs232b_esp_ws_t *adapter, gs232b_session_key_t session)
{
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        if (gs232b_session_key_equal(adapter->clients[i].session, session)) {
            return &adapter->clients[i];
        }
    }
    return NULL;
}

static gs232b_error_t send_bytes(void *ctx, gs232b_session_key_t session,
                                 const uint8_t *bytes, size_t size)
{
    gs232b_esp_ws_t *adapter = ctx;
    if (adapter == NULL || !gs232b_session_key_valid(session) ||
        (bytes == NULL && size != 0)) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }
    if (adapter->opening != NULL && !gs232b_session_key_valid(adapter->opening->session)) {
        adapter->opening->session = session;
    }
    gs232b_ws_client_t *client = find(adapter, session);
    if (client == NULL) { return GS232B_ERROR_STALE_SESSION; }
    if (atomic_load(&client->close_requested) || atomic_load(&client->http_closed)) {
        return GS232B_ERROR_CLOSED;
    }
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    const bool fits = size <= GS232B_WS_TX_BYTES - client->tx_count;
    if (fits) {
        if (size != 0 && !atomic_load(&client->send_pending)) {
            atomic_store(&client->send_started_ms, (uint32_t)((uint64_t)esp_timer_get_time() / 1000U));
            atomic_store(&client->send_pending, true);
        }
        for (size_t i = 0; i < size; ++i) {
            client->tx[(client->tx_head + client->tx_count + i) % GS232B_WS_TX_BYTES] = bytes[i];
        }
        client->tx_count += size;
    }
    (void)xSemaphoreGive(adapter->lock);
    return fits ? GS232B_OK : GS232B_ERROR_RESOURCE_EXHAUSTED;
}

static gs232b_error_t close_session(void *ctx, gs232b_session_key_t session)
{
    gs232b_esp_ws_t *adapter = ctx;
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    gs232b_ws_client_t *client = find(adapter, session);
    if (!gs232b_session_key_valid(session) || client == NULL) {
        return GS232B_ERROR_STALE_SESSION;
    }
    atomic_store(&client->close_requested, true);
    client->session = (gs232b_session_key_t){0};
    return GS232B_OK;
}

gs232b_session_io_t gs232b_esp_ws_io(gs232b_esp_ws_t *adapter)
{
    return (gs232b_session_io_t){.ctx = adapter, .send = send_bytes, .close = close_session};
}

gs232b_error_t gs232b_esp_ws_open(gs232b_esp_ws_t *adapter, gs232b_dispatcher_t *dispatcher)
{
    if (adapter == NULL || adapter->opened || adapter->lock != NULL ||
        dispatcher == NULL || !dispatcher->initialized) { return GS232B_ERROR_INVALID_ARGUMENT; }
    adapter->lock = xSemaphoreCreateMutex();
    if (adapter->lock == NULL) { return GS232B_ERROR_RESOURCE_EXHAUSTED; }
    adapter->dispatcher = dispatcher;
    adapter->uri_contexts[0] = (gs232b_ws_uri_context_t){adapter, false};
    adapter->uri_contexts[1] = (gs232b_ws_uri_context_t){adapter, true};
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        adapter->clients[i].adapter = adapter;
        adapter->clients[i].ready = xSemaphoreCreateBinary();
        if (adapter->clients[i].ready == NULL) {
            (void)gs232b_esp_ws_close(adapter);
            return GS232B_ERROR_RESOURCE_EXHAUSTED;
        }
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 3;
    config.max_open_sockets = GS232B_WS_CLIENTS;
    config.lru_purge_enable = false;
    config.recv_wait_timeout = config.send_wait_timeout = 1;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    esp_err_t error = httpd_start(&adapter->server, &config);
    if (error == ESP_OK) {
        const httpd_uri_t uri = {
            .uri = GS232B_WS_URI, .method = HTTP_GET, .handler = gs232b_ws_receive,
            .user_ctx = &adapter->uri_contexts[0], .is_websocket = true, .handle_ws_control_frames = true,
            .ws_pre_handshake_cb = gs232b_ws_pre_handshake,
            .ws_post_handshake_cb = gs232b_ws_post_handshake
        };
        error = httpd_register_uri_handler(adapter->server, &uri);
        if (error == ESP_OK) {
            const httpd_uri_t manage = {
                .uri = GS232B_WS_MANAGE_URI, .method = HTTP_GET, .handler = gs232b_ws_receive,
                .user_ctx = &adapter->uri_contexts[1], .is_websocket = true,
                .handle_ws_control_frames = true,
                .ws_pre_handshake_cb = gs232b_ws_pre_handshake,
                .ws_post_handshake_cb = gs232b_ws_post_handshake
            };
            error = httpd_register_uri_handler(adapter->server, &manage);
        }
        if (error == ESP_OK) {
            const httpd_uri_t page = {
                .uri = "/", .method = HTTP_GET,
                .handler = gs232b_provisioning_page, .user_ctx = adapter
            };
            error = httpd_register_uri_handler(adapter->server, &page);
        }
    }
    if (error != ESP_OK) {
        const gs232b_error_t cleanup = gs232b_esp_ws_close(adapter);
        return cleanup == GS232B_OK ? sdk_error(error) : cleanup;
    }
    adapter->opened = true;
    return GS232B_OK;
}

gs232b_error_t gs232b_esp_ws_interface(gs232b_esp_ws_t *adapter,
    gs232b_transport_t transport, uint32_t address, uint64_t epoch, const char *if_name)
{
    if (adapter == NULL || epoch == 0 ||
        (transport != GS232B_TRANSPORT_ETHERNET_WS && transport != GS232B_TRANSPORT_WIFI_WS &&
         transport != GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS)) {
        return GS232B_ERROR_INVALID_ARGUMENT;
    }
    gs232b_ws_interface_t next = {.address = address, .epoch = epoch};
    if (address != 0) {
        if (if_name == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
        size_t length = 0;
        while (length < sizeof(next.if_name) && if_name[length] != '\0') { ++length; }
        if (length == 0 || length == sizeof(next.if_name)) {
            return GS232B_ERROR_INVALID_ARGUMENT;
        }
        memcpy(next.if_name, if_name, length + 1U);
    }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    adapter->interfaces[transport - GS232B_TRANSPORT_ETHERNET_WS] = next;
    adapter->dispatcher->entries_available[transport] = address != 0;
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        gs232b_ws_client_t *client = &adapter->clients[i];
        if (atomic_load(&client->occupied) && client->transport == transport &&
            (client->interface.address != address || client->interface.epoch != epoch ||
             strcmp(client->interface.if_name, next.if_name) != 0)) {
            atomic_store(&client->close_requested, true);
        }
    }
    (void)xSemaphoreGive(adapter->lock);
    return GS232B_OK;
}

static gs232b_error_t lifecycle(gs232b_esp_ws_t *adapter, gs232b_ws_client_t *client)
{
    gs232b_error_t result = GS232B_OK;
    if (client->transport == GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS &&
        atomic_load(&client->session_ready)) {
        gs232b_provisioning_status_t status;
        (void)gs232b_provisioning_snapshot(&adapter->dispatcher->provisioning, &status);
        if ((status.state != GS232B_PROVISIONING_ACTIVE || status.remaining_us == 0) &&
            !atomic_load(&client->send_pending)) atomic_store(&client->close_requested, true);
    }
    const uint32_t now_ms = (uint32_t)((uint64_t)esp_timer_get_time() / 1000U);
    /* NOTE: 截止时间由控制任务独立检查。HTTPD 被慢 RX 占用、pump 尚未执行时，
     * 仍必须及时停止失联控制者；32 位毫秒差值允许单调计时器跨回绕。 */
    if (atomic_load(&client->session_ready) &&
        ((uint32_t)(now_ms - atomic_load(&client->transport_seen_ms)) >=
            GS232B_WS_PONG_TIMEOUT_US / 1000U ||
         (atomic_load(&client->send_pending) &&
          (uint32_t)(now_ms - atomic_load(&client->send_started_ms)) >=
            GS232B_WS_SEND_TIMEOUT_US / 1000U))) {
        atomic_store(&client->close_requested, true);
    }
    if (atomic_load(&client->close_requested) || atomic_load(&client->http_closed)) {
        if (gs232b_session_key_valid(client->session)) {
            result = gs232b_dispatcher_session_close(adapter->dispatcher, client->session);
        }
        if (atomic_exchange(&client->pending, false)) { (void)xSemaphoreGive(client->ready); }
        if (atomic_load(&client->http_closed) && !gs232b_session_key_valid(client->session)) {
            /* NOTE: HTTPD free_ctx 已返回前的最后一步是发布 http_closed；之后不再访问槽。 */
            client->rx_head = client->rx_count = client->tx_head = client->tx_count = 0;
            atomic_store(&client->http_closed, false);
            atomic_store(&client->session_ready, false);
            atomic_store(&client->close_requested, false);
            atomic_store(&client->occupied, false);
        }
        return result;
    }
    if (atomic_exchange(&client->pending, false)) {
        adapter->opening = client;
        gs232b_session_key_t session;
        result = client->management ? gs232b_dispatcher_management_session_open(
            adapter->dispatcher, client->transport, client->line, sizeof(client->line), &session) :
            gs232b_dispatcher_session_open(adapter->dispatcher, client->transport,
                client->line, sizeof(client->line), &session);
        adapter->opening = NULL;
        if (result == GS232B_OK && !gs232b_session_key_equal(session, client->session)) {
            (void)gs232b_dispatcher_session_close(adapter->dispatcher, session);
            result = GS232B_ERROR_INVALID_ARGUMENT;
        }
        if (result == GS232B_OK) {
            /* NOTE: session_ready 发布前 HTTPD 尚不读取 rx_context，无需等待共享锁。 */
            result = gs232b_dispatcher_capture(adapter->dispatcher, session, &client->rx_context);
        }
        atomic_store(&client->session_ready, result == GS232B_OK);
        if (result != GS232B_OK) { atomic_store(&client->close_requested, true); }
        (void)xSemaphoreGive(client->ready);
    }
    return result;
}

gs232b_error_t gs232b_esp_ws_service(gs232b_esp_ws_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    if (!adapter->opened) { return GS232B_ERROR_NOT_READY; }
    gs232b_error_t result = GS232B_OK;
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        if (!atomic_load(&adapter->clients[i].occupied)) { continue; }
        const gs232b_error_t next = lifecycle(adapter, &adapter->clients[i]);
        if (result == GS232B_OK) { result = next; }
    }
    gs232b_ws_packet_t packet = {0};
    if (xSemaphoreTake(adapter->lock, 0) != pdPASS) { return GS232B_ERROR_WOULD_BLOCK; }
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        gs232b_ws_client_t *client = &adapter->clients[i];
        if (gs232b_session_key_valid(client->session)) {
            (void)gs232b_dispatcher_capture(adapter->dispatcher, client->session, &client->rx_context);
        }
    }
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        const size_t index = (adapter->cursor + i) % GS232B_WS_CLIENTS;
        gs232b_ws_client_t *client = &adapter->clients[index];
        if (atomic_load(&client->session_ready) && !atomic_load(&client->close_requested) &&
            !atomic_load(&client->http_closed) && client->rx_count != 0) {
            packet = client->rx[client->rx_head];
            client->rx_head = (client->rx_head + 1U) % GS232B_WS_RX_PACKETS;
            --client->rx_count;
            adapter->cursor = (index + 1U) % GS232B_WS_CLIENTS;
            break;
        }
    }
    (void)xSemaphoreGive(adapter->lock);
    if (packet.size != 0) {
        const gs232b_error_t next = gs232b_dispatcher_feed(adapter->dispatcher,
            packet.context, packet.bytes, packet.size);
        if (result == GS232B_OK) { result = next; }
    }
    if (!atomic_exchange(&adapter->pump_pending, true)) {
        if (httpd_queue_work(adapter->server, gs232b_ws_pump, adapter) != ESP_OK) {
            atomic_store(&adapter->pump_pending, false);
            if (result == GS232B_OK) { result = GS232B_ERROR_WOULD_BLOCK; }
        }
    }
    return result;
}

gs232b_error_t gs232b_esp_ws_close(gs232b_esp_ws_t *adapter)
{
    if (adapter == NULL) { return GS232B_ERROR_INVALID_ARGUMENT; }
    gs232b_error_t result = GS232B_OK;
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        if (gs232b_session_key_valid(adapter->clients[i].session)) {
            const gs232b_error_t next = gs232b_dispatcher_session_close(
                adapter->dispatcher, adapter->clients[i].session);
            if (result == GS232B_OK) { result = next; }
        }
    }
    if (adapter->server != NULL) {
        const esp_err_t error = httpd_stop(adapter->server);
        if (error != ESP_OK) { return sdk_error(error); }
    }
    for (size_t i = 0; i < GS232B_WS_CLIENTS; ++i) {
        if (adapter->clients[i].ready != NULL) { vSemaphoreDelete(adapter->clients[i].ready); }
    }
    if (adapter->dispatcher != NULL) {
        for (size_t i = GS232B_TRANSPORT_ETHERNET_WS; i <= GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS; ++i) {
            adapter->dispatcher->entries_available[i] = false;
        }
    }
    if (adapter->lock != NULL) { vSemaphoreDelete(adapter->lock); }
    memset(adapter, 0, sizeof(*adapter));
    return result;
}
