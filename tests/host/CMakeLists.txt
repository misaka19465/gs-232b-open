cmake_minimum_required(VERSION 3.22)

# This is a standalone native project: do not include the ESP-IDF project here.
project(gs_232b_open_host_tests LANGUAGES C)

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Host tests require a native C compiler")
endif()

enable_testing()

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
                             command_invalidation_tests)
    add_executable(${motion_test}
        ${motion_test}.c
        ../fakes/motion_sim.c
        ../../src/core/types.c
        ../../src/core/ports.c
        ../../src/core/coordinates.c
        ../../src/core/feedback.c
        ../../src/core/persistence.c
        ../../src/core/output_manager.c
        ../../src/core/motion.c
        ../../src/core/motion_control.c
        ../../src/core/motion_feedback.c
        ../../src/core/line_parser.c
        ../../src/core/command_control.c
        ../../src/core/command_input.c
        ../../src/core/command_dispatch.c
    )
    target_include_directories(${motion_test} PRIVATE ../../include ../fakes)
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
