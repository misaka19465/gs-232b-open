idf_component_register(
    SRCS "app_main.c" "core/types.c" "core/ports.c" "core/line_parser.c"
         "core/response_encoder.c" "core/coordinates.c" "core/feedback.c"
         "core/persistence.c" "core/output_manager.c" "core/motion.c" "core/speed.c"
         "core/motion_control.c" "core/motion_feedback.c" "platform/nvs_store.c"
         "core/command_control.c" "core/command_input.c" "core/command_dispatch.c"
         "core/position_parser.c" "core/sequence_store.c" "core/command_upload.c"
         "core/command_sequence.c" "core/command_calibration.c"
         "core/uart_baud.c" "core/uart_baud_store.c"
         "core/ethernet.c" "platform/esp_ethernet.c"
         "core/wifi_sta.c" "platform/esp_wifi_sta.c"
         "platform/esp_wifi_sta_driver.c" "platform/esp_wifi_sta_events.c"
         "platform/esp_ethernet_driver.c" "platform/esp_ethernet_events.c"
         "core/gs_dispatcher.c" "core/gs_commands.c" "core/gs_replies.c"
         "platform/shared_spi.c" "platform/w5500_reset.c"
         "platform/esp_shared_spi.c" "platform/esp_w5500.c" "platform/esp_adc.c"
         "platform/esp_ledc.c" "platform/esp_uart.c" "platform/esp_uart_rx.c"
         "platform/esp_uart_capture.c" "platform/esp_uart_boot.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_uart esp_eth esp_timer esp_adc
             nvs_flash esp_wifi w5500 esp_driver_rmt esp_netif esp_event lwip esp_hw_support
)
