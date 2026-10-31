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
