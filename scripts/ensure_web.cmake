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
