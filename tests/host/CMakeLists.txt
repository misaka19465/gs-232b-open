cmake_minimum_required(VERSION 3.22)

# This is a standalone native project: do not include the ESP-IDF project here.
project(gs_232b_open_host_tests LANGUAGES C)

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Host tests require a native C compiler")
endif()

enable_testing()

find_package(Threads REQUIRED)

add_executable(esp_ledc_tests
    esp_ledc_tests.c
    ../fakes/esp_idf/ledc_api.c
    ../../src/platform/esp_ledc.c
)
target_include_directories(esp_ledc_tests PRIVATE ../../include ../fakes/esp_idf)
target_compile_definitions(esp_ledc_tests PRIVATE ESP_PLATFORM)
set_target_properties(esp_ledc_tests PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(esp_ledc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME esp_ledc COMMAND esp_ledc_tests)
set_tests_properties(esp_ledc PROPERTIES TIMEOUT 5)

add_test(NAME ledc_rejects_uart0_console COMMAND ${CMAKE_C_COMPILER}
    -std=c11 -DESP_PLATFORM -DCONFIG_ESP_CONSOLE_UART=1 -DCONFIG_ESP_CONSOLE_UART_NUM=0
    -I${CMAKE_CURRENT_SOURCE_DIR}/../../include
    -I${CMAKE_CURRENT_SOURCE_DIR}/../fakes/esp_idf
    -c ${CMAKE_CURRENT_SOURCE_DIR}/../../src/platform/esp_ledc.c
    -o ${CMAKE_CURRENT_BINARY_DIR}/ledc_uart0_rejected.o)
set_tests_properties(ledc_rejects_uart0_console PROPERTIES WILL_FAIL TRUE TIMEOUT 5)

foreach(console IN ITEMS uart mirror no_usb)
    if(console STREQUAL "uart")
        set(console_flags -DCONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=1
            -DCONFIG_ESP_CONSOLE_SECONDARY_NONE=1 -DCONFIG_ESP_CONSOLE_UART=1)
    elseif(console STREQUAL "mirror")
        set(console_flags -DCONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=1
            -DCONFIG_ESP_CONSOLE_SECONDARY_NONE=0)
    else()
        set(console_flags -DCONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=0
            -DCONFIG_ESP_CONSOLE_SECONDARY_NONE=1)
    endif()
    add_test(NAME uart_rejects_${console}_console COMMAND ${CMAKE_C_COMPILER}
        -std=c11 -DESP_PLATFORM ${console_flags}
        -I${CMAKE_CURRENT_SOURCE_DIR}/../../include
        -I${CMAKE_CURRENT_SOURCE_DIR}/../fakes/esp_idf
        -c ${CMAKE_CURRENT_SOURCE_DIR}/../../src/platform/esp_uart.c
        -o ${CMAKE_CURRENT_BINARY_DIR}/uart_${console}_rejected.o)
    set_tests_properties(uart_rejects_${console}_console PROPERTIES WILL_FAIL TRUE TIMEOUT 5)
endforeach()

add_executable(speed_tests speed_tests.c ../../src/core/speed.c)
target_include_directories(speed_tests PRIVATE ../../include)
set_target_properties(speed_tests PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(speed_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME speed_levels COMMAND speed_tests)
set_tests_properties(speed_levels PROPERTIES TIMEOUT 5)

add_executable(esp_ledc_output_tests
    esp_ledc_output_tests.c ../fakes/esp_idf/ledc_api.c ../fakes/spi_bus.c
    ../../src/platform/esp_ledc.c ../../src/platform/shared_spi.c
    ../../src/core/types.c ../../src/core/ports.c ../../src/core/output_manager.c
)
target_include_directories(esp_ledc_output_tests PRIVATE ../../include ../fakes/esp_idf ../fakes)
target_compile_definitions(esp_ledc_output_tests PRIVATE ESP_PLATFORM)
target_link_libraries(esp_ledc_output_tests PRIVATE Threads::Threads)
set_target_properties(esp_ledc_output_tests PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(esp_ledc_output_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME esp_ledc_outputs COMMAND esp_ledc_output_tests)
set_tests_properties(esp_ledc_outputs PROPERTIES TIMEOUT 5)

foreach(adc_test IN ITEMS esp_adc_tests esp_adc_motion_tests)
    add_executable(${adc_test}
        ${adc_test}.c
        ../fakes/esp_idf/adc_api.c
        ../fakes/esp_idf/adc_queue.c
        ../../src/platform/esp_adc.c
        ../../src/core/types.c
        ../../src/core/ports.c
        ../../src/core/coordinates.c
        ../../src/core/feedback.c
        ../../src/core/persistence.c
        ../../src/core/output_manager.c
        ../../src/core/motion.c
        ../../src/core/speed.c
        ../../src/core/motion_control.c
        ../../src/core/motion_feedback.c
    )
    # NOTE: IDF API 替身仅在这两个原生目标可见，直接编译实际板级适配器。
    target_include_directories(${adc_test} PRIVATE ../../include ../fakes/esp_idf)
    target_compile_definitions(${adc_test} PRIVATE ESP_PLATFORM)
    target_link_libraries(${adc_test} PRIVATE Threads::Threads)
    set_target_properties(${adc_test} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${adc_test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${adc_test} COMMAND ${adc_test})
    set_tests_properties(${adc_test} PROPERTIES TIMEOUT 15)
endforeach()

foreach(spi_test IN ITEMS shared_spi_tests w5500_reset_tests)
    add_executable(${spi_test}
        ${spi_test}.c
        ../fakes/spi_bus.c
        ../../src/platform/shared_spi.c
        ../../src/platform/w5500_reset.c
        ../../src/core/output_manager.c
        ../../src/core/types.c
        ../../src/core/ports.c
    )
    target_include_directories(${spi_test} PRIVATE ../../include ../fakes)
    target_link_libraries(${spi_test} PRIVATE Threads::Threads)
    set_target_properties(${spi_test} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${spi_test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${spi_test} COMMAND ${spi_test})
    set_tests_properties(${spi_test} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(app_main_smoke app_main_smoke.c ../../src/app_main.c)
set_target_properties(app_main_smoke PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(app_main_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME app_main_returns COMMAND app_main_smoke)
set_tests_properties(app_main_returns PROPERTIES TIMEOUT 5)

add_executable(core_contract_tests
    core_contract_tests.c
    ../fakes/fakes.c
    ../../src/core/types.c
    ../../src/core/ports.c
)
target_include_directories(core_contract_tests PRIVATE
    ../../include
    ../fakes
)
set_target_properties(core_contract_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(core_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME core_contracts COMMAND core_contract_tests)
set_tests_properties(core_contracts PROPERTIES TIMEOUT 5)

add_executable(output_manager_tests
    output_manager_tests.c
    ../../src/core/types.c
    ../../src/core/ports.c
    ../../src/core/output_manager.c
)
target_include_directories(output_manager_tests PRIVATE
    ../../include
    ../fakes
)
set_target_properties(output_manager_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(output_manager_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME output_manager COMMAND output_manager_tests)
set_tests_properties(output_manager PROPERTIES TIMEOUT 5)

add_executable(line_parser_tests
    line_parser_tests.c
    ../../src/core/line_parser.c
    ../../src/core/position_parser.c
)
target_include_directories(line_parser_tests PRIVATE ../../include)
set_target_properties(line_parser_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(line_parser_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME line_parser COMMAND line_parser_tests)
set_tests_properties(line_parser PROPERTIES TIMEOUT 5)

add_executable(response_encoder_tests
    response_encoder_tests.c
    ../../src/core/response_encoder.c
)
target_include_directories(response_encoder_tests PRIVATE ../../include)
set_target_properties(response_encoder_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(response_encoder_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME response_encoder COMMAND response_encoder_tests)
set_tests_properties(response_encoder PROPERTIES TIMEOUT 5)

add_executable(coordinates_tests
    coordinates_tests.c
    ../../src/core/coordinates.c
)
target_include_directories(coordinates_tests PRIVATE ../../include)
set_target_properties(coordinates_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(coordinates_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME coordinates COMMAND coordinates_tests)
set_tests_properties(coordinates PROPERTIES TIMEOUT 5)

add_executable(feedback_tests
    feedback_tests.c
    ../fakes/fakes.c
    ../../src/core/types.c
    ../../src/core/ports.c
    ../../src/core/coordinates.c
    ../../src/core/feedback.c
    ../../src/core/persistence.c
)
target_include_directories(feedback_tests PRIVATE
    ../../include
    ../fakes
)
set_target_properties(feedback_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(feedback_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME feedback COMMAND feedback_tests)
set_tests_properties(feedback PROPERTIES TIMEOUT 5)

add_executable(persistence_tests
    persistence_tests.c
    ../fakes/fakes.c
    ../../src/core/types.c
    ../../src/core/ports.c
    ../../src/core/coordinates.c
    ../../src/core/feedback.c
    ../../src/core/persistence.c
)
target_include_directories(persistence_tests PRIVATE
    ../../include
    ../fakes
)
set_target_properties(persistence_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(persistence_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME persistence COMMAND persistence_tests)
set_tests_properties(persistence PROPERTIES TIMEOUT 5)

foreach(motion_test IN ITEMS motion_tests motion_fault_tests command_control_tests
                             command_invalidation_tests sequence_upload_tests
                             sequence_commit_tests sequence_timing_tests
                             sequence_lifecycle_tests sequence_fault_tests
                             calibration_tests calibration_lifecycle_tests
                             gs_commands_tests gs_sessions_tests gs_calibration_tests
                             gs_sequences_tests esp_uart_tests transport_fault_tests)
    add_executable(${motion_test}
        ${motion_test}.c
        ../fakes/motion_sim.c
        ../fakes/fakes.c
        ../../src/core/types.c
        ../../src/core/ports.c
        ../../src/core/coordinates.c
        ../../src/core/feedback.c
        ../../src/core/persistence.c
        ../../src/core/output_manager.c
        ../../src/core/motion.c
        ../../src/core/speed.c
        ../../src/core/motion_control.c
        ../../src/core/motion_feedback.c
        ../../src/core/line_parser.c
        ../../src/core/position_parser.c
        ../../src/core/sequence_store.c
        ../../src/core/command_sequence.c
        ../../src/core/command_calibration.c
        ../../src/core/response_encoder.c
        ../../src/core/command_upload.c
        ../../src/core/command_control.c
        ../../src/core/command_input.c
        ../../src/core/command_dispatch.c
        ../../src/core/gs_dispatcher.c
        ../../src/core/gs_commands.c
        ../../src/core/gs_replies.c
    )
    target_include_directories(${motion_test} PRIVATE ../../include ../fakes)
    if(motion_test STREQUAL "esp_uart_tests")
        target_sources(${motion_test} PRIVATE ../../src/platform/esp_uart.c
            ../../src/platform/esp_uart_rx.c ../fakes/esp_idf/uart_api.c
            ../fakes/esp_idf/uart_rtos.c)
        target_include_directories(${motion_test} PRIVATE ../fakes/esp_idf ../../src/platform)
        target_compile_definitions(${motion_test} PRIVATE ESP_PLATFORM
            CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=1 CONFIG_ESP_CONSOLE_SECONDARY_NONE=1)
        target_link_libraries(${motion_test} PRIVATE Threads::Threads)
    endif()
    set_target_properties(${motion_test} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${motion_test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${motion_test} COMMAND ${motion_test})
    set_tests_properties(${motion_test} PROPERTIES TIMEOUT 5)
endforeach()
