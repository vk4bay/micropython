set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.spiram_oct
    boards/BDARS_S3/sdkconfig.board
)
# Force SPIRAM pins via CMake
set(CONFIG_SPIRAM_CLK_IO 33 CACHE STRING "" FORCE)
set(CONFIG_SPIRAM_CS_IO 28 CACHE STRING "" FORCE)


list(APPEND MICROPY_SOURCE_PORT
    ${PROJECT_DIR}/modules_custom/ili9488.c
    ${PROJECT_DIR}/modules_custom/ft6336.c
    ${PROJECT_DIR}/modules_custom/ili9488_ui.c
)

