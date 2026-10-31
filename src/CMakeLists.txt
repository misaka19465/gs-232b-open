idf_component_register(
    SRCS "app_main.c" "core/types.c" "core/ports.c" "core/line_parser.c"
         "core/response_encoder.c" "core/coordinates.c" "core/feedback.c"
         "core/persistence.c" "core/output_manager.c" "core/motion.c"
         "core/motion_control.c" "core/motion_feedback.c" "platform/nvs_store.c"
         "core/command_control.c" "core/command_input.c" "core/command_dispatch.c"
         "core/position_parser.c" "core/sequence_store.c" "core/command_upload.c"
         "core/command_sequence.c" "core/command_calibration.c"
    INCLUDE_DIRS "." "../include"
)
