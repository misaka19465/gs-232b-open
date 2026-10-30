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
