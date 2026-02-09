cmake_minimum_required(VERSION 3.12)

# Project name
set(NAME tufty_badge)

# Set the Pimoroni Pico path
set(PIMORONI_PICO_PATH ${CMAKE_CURRENT_LIST_DIR}/pimoroni-pico)

include(pico_sdk_import.cmake)
include(pimoroni_pico_import.cmake)

project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Add pimoroni-pico include directory for headers
include_directories(${PIMORONI_PICO_PATH})

# Add required subdirectories from pimoroni-pico
# pico_graphics includes bitmap_fonts and hershey_fonts, so don't add them separately
add_subdirectory(${PIMORONI_PICO_PATH}/common ${CMAKE_BINARY_DIR}/pimoroni-common)
add_subdirectory(${PIMORONI_PICO_PATH}/drivers/st7789 ${CMAKE_BINARY_DIR}/pimoroni-st7789)
add_subdirectory(${PIMORONI_PICO_PATH}/libraries/pico_graphics ${CMAKE_BINARY_DIR}/pimoroni-pico_graphics)
add_subdirectory(${PIMORONI_PICO_PATH}/libraries/tufty2040 ${CMAKE_BINARY_DIR}/pimoroni-tufty2040)
add_subdirectory(${PIMORONI_PICO_PATH}/libraries/pngdec ${CMAKE_BINARY_DIR}/pimoroni-pngdec)

# Add LittleFS for flash filesystem support
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/pico-littlefs/littlefs-lib ${CMAKE_BINARY_DIR}/littlefs-lib)

# Add your source files
add_executable(${NAME}
    main.cpp
)

# Link libraries
target_link_libraries(${NAME}
    pico_stdlib
    hardware_pwm
    hardware_gpio
    hardware_flash
    tufty2040
    pico_graphics
    st7789
    pngdec
    littlefs-lib
)

# Enable USB output for debugging
pico_enable_stdio_usb(${NAME} 1)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
