cmake_minimum_required(VERSION 3.18)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_SDK_PATH}/tools/CMakeLists.txt)

project(DSP C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()


add_executable(DSP
        main.cpp
        )

target_link_libraries(DSP
		pico_stdlib
		hardware_adc
		hardware_dma
    hardware_pwm
		)

pico_add_extra_outputs(DSP)
