idf_component_register(
    SRCS "ov02c10.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_cam_sensor esp_sccb_intf esp_driver_gpio esp_driver_cam esp_timer
)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov02c10_detect")
