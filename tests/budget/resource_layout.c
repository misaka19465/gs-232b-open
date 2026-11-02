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

#include "app/esp_app_internal.h"
#include "driver/uart.h"

/* NOTE: 仅编译为测量对象，不链接进固件；符号长度由真实 C3 ABI 和 SDK 计算。 */
#define BUDGET(name, expression) unsigned char gs232b_budget_##name[(expression)]
BUDGET(runtime, sizeof(gs232b_esp_app_t));
BUDGET(dispatcher, sizeof(gs232b_dispatcher_t));
BUDGET(request_pool, sizeof(((gs232b_app_t *)0)->requests));
BUDGET(sequence_banks, sizeof(((gs232b_sequence_store_t *)0)->banks));
BUDGET(ws_adapter, sizeof(gs232b_esp_ws_t));
BUDGET(ws_client, sizeof(gs232b_ws_client_t));
BUDGET(ws_rx_slots, sizeof(((gs232b_ws_client_t *)0)->rx) * GS232B_WS_CLIENTS);
BUDGET(ws_tx, GS232B_WS_TX_BYTES * GS232B_WS_CLIENTS);
BUDGET(ws_lines, GS232B_LINE_STORAGE_BYTES * GS232B_WS_CLIENTS);
BUDGET(ws_controls, sizeof(((gs232b_ws_client_t *)0)->controls) * GS232B_WS_CLIENTS);
BUDGET(ws_wire, sizeof(((gs232b_ws_client_t *)0)->wire) * GS232B_WS_CLIENTS);
BUDGET(uart_adapter, sizeof(gs232b_esp_uart_t));
BUDGET(uart_line, GS232B_LINE_STORAGE_BYTES);
BUDGET(uart_driver_rx, GS232B_UART_RX_RING_BYTES);
BUDGET(uart_packet_payload, GS232B_UART_RX_PACKETS * sizeof(gs232b_uart_packet_t));
BUDGET(uart_event_payload, GS232B_UART_EVENTS * sizeof(uart_event_t));
BUDGET(network_event_payload, 2U * GS232B_NETWORK_EVENT_CAPACITY * sizeof(gs232b_esp_network_event_t));
BUDGET(network_fence_payload, 2U * sizeof(uint64_t));
BUDGET(network_work_payload, 2U * (sizeof(unsigned) + sizeof(gs232b_error_t)));
BUDGET(app_mailbox_payload, sizeof(gs232b_net_config_t) + sizeof(gs232b_app_ap_request_t));
BUDGET(adc_mailbox_payload, GS232B_AXIS_COUNT * sizeof(gs232b_sample_t));
BUDGET(store_worker, sizeof(gs232b_esp_store_worker_t));
BUDGET(store_cache_slots, 2U * GS232B_STORE_DOMAIN_COUNT * sizeof(gs232b_store_cache_t));
BUDGET(queue_control, sizeof(StaticQueue_t));
BUDGET(task_control, sizeof(StaticTask_t));

_Static_assert(GS232B_WS_CLIENTS == 4U, "Review connection budget");
_Static_assert(GS232B_CONTROL_MAX_SESSIONS == 7U, "Review core session budget");
_Static_assert(GS232B_CONTROL_MAX_REQUESTS == 56U, "Review request budget");
_Static_assert(GS232B_LINE_STORAGE_BYTES == 2048U, "Review per-session workspace budget");
_Static_assert(sizeof(gs232b_esp_app_t) <= 176U * 1024U, "Runtime exceeds static RAM envelope");
