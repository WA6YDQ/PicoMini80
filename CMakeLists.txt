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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# select board (pico, pico_w, pico_2, etc.)
set(PICO_BOARD pico2)

# added
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include(/opt/pico-extras/external/pico_extras_import.cmake) # Added for dormant



project(emulator_1 C CXX ASM)



pico_sdk_init()

# these are all the source files
add_executable(${PROJECT_NAME} emulator_1.c hw_config.c)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)

# library subdirectories
add_subdirectory(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src)

# test memory usage - display at build time
string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--print-memory-usage")

# added
pico_set_program_name(emulator_1 "emulator_v1")
pico_set_program_version(emulator_1 "0.1")


target_link_libraries(${PROJECT_NAME} 
			pico_stdlib 
			pico_util
			pico_multicore
			hardware_dma
			hardware_pio
			hardware_timer
			hardware_clocks
			hardware_pwm
			hardware_gpio
			hardware_spi
			no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
			)
			
pico_add_extra_outputs(${PROJECT_NAME})

