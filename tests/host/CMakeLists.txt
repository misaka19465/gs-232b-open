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
