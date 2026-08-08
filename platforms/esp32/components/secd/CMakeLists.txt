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
    INCLUDE_DIRS
        "${SECD_INC_DIR}"
    PRIV_REQUIRES
        esp_driver_gpio esp_driver_uart esp_timer esp_system freertos
)

# Feature macros must match this board's primitive IDs in the metadata.
# esp32c3-supermini enables GPIO + UART (uart ids 18-20).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SECD_FEATURE_GPIO=1
    SECD_FEATURE_UART=1
    SECD_FEATURES_STR="gpio,uart")