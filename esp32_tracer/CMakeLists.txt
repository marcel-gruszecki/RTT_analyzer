idf_component_register(
    SRCS        "task_tracer.c"
    INCLUDE_DIRS "."
    REQUIRES    freertos esp_timer esp_hw_support
)

# SEGGER RTT — add the two source files directly from the SDK.
# Adjust the path if your ESP-IDF is in a different location.
set(RTT_DIR "$ENV{IDF_PATH}/components/app_trace/sys_view/SEGGER")
target_sources(${COMPONENT_LIB} PRIVATE
    ${RTT_DIR}/SEGGER_RTT.c
    ${RTT_DIR}/SEGGER_RTT_printf.c
)
target_include_directories(${COMPONENT_LIB} PUBLIC ${RTT_DIR})
