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

#ifndef GS232B_GS_HELP_FIXTURES_H
#define GS232B_GS_HELP_FIXTURES_H

static const char gs_help_azimuth[] =
    "R / L : Azimuth clockwise / counterclockwise\r\n"
    "A : Stop azimuth\r\n"
    "S : Stop all and clear sequence\r\n"
    "Maaa : Set azimuth\r\n"
    "Msss aaa bbb ... : Load azimuth sequence\r\n"
    "C / C2 : Read azimuth / azimuth and elevation\r\n"
    "X1 / X2 / X3 / X4 : Azimuth speed\r\n"
    "T / N : Start sequence / read sequence index and count\r\n"
    "O / F : Azimuth offset / calibration display\r\n"
    "H / H2 / H3 : Azimuth / elevation / display help\r\n";

static const char gs_help_elevation[] =
    "U / D : Elevation up / down\r\n"
    "E : Stop elevation\r\n"
    "S : Stop all and clear sequence\r\n"
    "Waaa eee : Set azimuth and elevation\r\n"
    "Wsss aaa eee bbb fff ... : Load azimuth/elevation sequence\r\n"
    "B / C2 : Read elevation / azimuth and elevation\r\n"
    "T / N : Start sequence / read sequence index and count\r\n"
    "O2 / F2 : Elevation offset / calibration display\r\n";

#define GS_HELP_DISPLAY_PREFIX \
    "P36 : 360 Degree mode\r\n" \
    "P45 : 450 Degree mode\r\n" \
    "Z : Toggle N/S center in 360 Degree mode\r\n"

#endif
