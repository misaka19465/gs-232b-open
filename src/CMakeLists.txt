idf_component_register(
    SRCS "app_main.c" "core/types.c" "core/ports.c" "core/line_parser.c"
         "core/response_encoder.c" "core/coordinates.c"
    INCLUDE_DIRS "." "../include"
)
