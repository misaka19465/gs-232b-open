# GS-232B Open - rotator controller firmware.
# Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
#
# SPDX-License-Identifier: GPL-3.0-only
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

include("${CMAKE_CURRENT_LIST_DIR}/../scripts/ensure_web.cmake")

idf_component_register(
    SRCS "app_main.c" "app/app.c" "app/esp_app.c" "app/esp_sampling.c"
         "app/esp_network.c" "app/esp_network_service.c" "platform/esp_store_worker.c"
         "core/types.c" "core/ports.c" "core/line_parser.c"
         "core/extension.c" "core/extension_json.c" "core/gs_extensions.c" "core/gs_management.c" "core/gs_admin.c"
         "core/network_config.c" "core/network_fields.c" "core/network_replies.c" "core/gs_network.c"
         "core/response_encoder.c" "core/coordinates.c" "core/feedback.c"
         "core/persistence.c" "core/output_manager.c" "core/motion.c" "core/speed.c"
         "core/rotor_config.c" "core/rotor_store.c" "core/gs_rotor.c"
         "core/motion_control.c" "core/motion_feedback.c" "platform/nvs_store.c"
         "core/command_control.c" "core/command_input.c" "core/command_dispatch.c"
         "core/position_parser.c" "core/sequence_store.c" "core/command_upload.c"
         "core/command_sequence.c" "core/command_calibration.c"
         "core/uart_baud.c" "core/uart_baud_store.c"
         "core/ethernet.c" "platform/esp_ethernet.c"
         "core/wifi_sta.c" "core/provisioning.c" "core/provisioning_scan.c" "platform/esp_wifi_sta.c"
         "platform/esp_wifi_sta_driver.c" "platform/esp_wifi_sta_events.c"
         "platform/esp_ethernet_driver.c" "platform/esp_ethernet_events.c"
         "core/gs_dispatcher.c" "core/gs_commands.c" "core/gs_replies.c"
         "platform/shared_spi.c" "platform/w5500_reset.c"
         "platform/esp_shared_spi.c" "platform/esp_w5500.c" "platform/esp_adc.c"
         "platform/esp_ledc.c" "platform/esp_uart.c" "platform/esp_uart_rx.c"
         "platform/esp_uart_capture.c" "platform/esp_uart_boot.c"
         "platform/esp_ws.c" "platform/esp_ws_rx.c" "platform/esp_ws_tx.c"
         "platform/provisioning_page.c"
         "platform/management_crypto.c"
         "core/management_replies.c"
    INCLUDE_DIRS "." "../include"
    EMBED_FILES "web/provisioning.html.gz"
    REQUIRES esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_uart esp_eth esp_timer esp_adc
             nvs_flash esp_wifi w5500 esp_driver_rmt esp_netif esp_event lwip esp_hw_support
             esp_http_server mbedtls esp_app_format
)
