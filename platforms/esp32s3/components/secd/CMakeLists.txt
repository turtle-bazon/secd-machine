# SECD VM component: compiles the shared core + ESP32 HAL into the app.
# Sources live in the repo, referenced from the component dir (../../../../ = repo).
set(SECD_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../src")
set(SECD_INC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../include")

idf_component_register(
    SRCS
        "${SECD_SRC_DIR}/core/heap.cpp"
        "${SECD_SRC_DIR}/core/gc.cpp"
        "${SECD_SRC_DIR}/core/machine.cpp"
        "${SECD_SRC_DIR}/core/bytecode.cpp"
        "${SECD_SRC_DIR}/core/primitives.cpp"
        "${SECD_SRC_DIR}/core/symbols.cpp"
        "${SECD_SRC_DIR}/hal/esp32.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/secd_start.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/secd_bytecode.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/usb.cpp"
    INCLUDE_DIRS
        "${SECD_INC_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}"
    PRIV_REQUIRES
        cherry-embedded__cherryusb esp_driver_gpio esp_driver_uart esp_driver_i2c esp_timer esp_system freertos
)

# Feature macros must match this board's primitive IDs in the metadata.
# stamp-s3a enables GPIO + UART + I2C + USB (i2c ids 26-29, usb/hid ids 30-37).
# SECD_DEBUG_BUILD = 1: print startup banner/waits; 0 = silent (release).
option(SECD_DEBUG_BUILD "Print startup banner in debug builds" ON)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SECD_FEATURE_GPIO=1
    SECD_FEATURE_UART=1
    SECD_FEATURE_I2C=1
    SECD_FEATURE_HID=1
    SECD_FEATURES_STR="gpio,uart,i2c,usb"
    SECD_DEBUG_BUILD=$<BOOL:${SECD_DEBUG_BUILD}>)