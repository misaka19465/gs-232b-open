set(GS232B_WEB_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(GS232B_WEB_HTML "${GS232B_WEB_ROOT}/src/web/provisioning.html")
set(GS232B_WEB_GZIP "${GS232B_WEB_ROOT}/src/web/provisioning.html.gz")

if(NOT EXISTS "${GS232B_WEB_HTML}" OR NOT EXISTS "${GS232B_WEB_GZIP}")
    find_program(GS232B_NPM_EXECUTABLE npm)
    if(NOT GS232B_NPM_EXECUTABLE)
        message(FATAL_ERROR "Web assets are missing; install Node.js/npm and run npm ci")
    endif()
    execute_process(
        COMMAND "${GS232B_NPM_EXECUTABLE}" run build:web
        WORKING_DIRECTORY "${GS232B_WEB_ROOT}"
        RESULT_VARIABLE GS232B_WEB_BUILD_RESULT
    )
    if(NOT GS232B_WEB_BUILD_RESULT EQUAL 0 OR
       NOT EXISTS "${GS232B_WEB_HTML}" OR NOT EXISTS "${GS232B_WEB_GZIP}")
        message(FATAL_ERROR "Could not generate the provisioning HTML assets; run npm ci")
    endif()
endif()
