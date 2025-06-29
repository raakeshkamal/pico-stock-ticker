# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)
include(secrets.cmake)

project(pico-stock-ticker C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(common)
add_subdirectory(drivers)
add_subdirectory(libraries)

# Add executable. Default name is the project name, version 0.1
add_executable(pico-stock-ticker
        pico-stock-ticker.cpp
        tls_common.c
        display.cpp
        )

pico_set_program_name(pico-stock-ticker "pico-stock-ticker")
pico_set_program_version(pico-stock-ticker "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(pico-stock-ticker 0)
pico_enable_stdio_usb(pico-stock-ticker 1)

# Add the standard library to the build
target_link_libraries(pico-stock-ticker
        pico_lwip_http
        pico_lwip_mbedtls
        pico_mbedtls
        pico_stdlib
        hardware_spi
        hardware_pwm
        hardware_dma
        hardware_rtc
        pico_rand
        FreeRTOS-Kernel-Heap3
        button
        rgbled
        st7789
        pico_graphics
        pico_display_2
        pico_cyw43_arch_lwip_sys_freertos
        ArduinoJson
        )

# Add the standard include files to the build
target_include_directories(pico-stock-ticker PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
)

target_compile_definitions(pico-stock-ticker PRIVATE
        NO_SYS=0 # don't want NO_SYS (generally this would be in your lwipopts.h)
        # ALTCP_MBEDTLS_AUTHMODE=MBEDTLS_SSL_VERIFY_REQUIRED
        CYW43_TASK_STACK_SIZE=2048
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        API_KEY=\"${API_KEY}\"
        ALTCP_MBEDTLS_AUTHMODE=MBEDTLS_SSL_VERIFY_REQUIRED
        # DEBUG
        )

# Ignore warnings from lwip code
set_source_files_properties(
        ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
        PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-result"
        )

pico_add_extra_outputs(pico-stock-ticker)