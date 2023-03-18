# pico-infonesPlus 
cmake_minimum_required(VERSION 3.13)


set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


add_definitions( -DNDEBUG )

include(pico_sdk_import.cmake)

project(piconesPlus C CXX ASM)
pico_sdk_init()


add_executable(piconesPlus
    main.cpp
)

pico_set_program_name(piconesPlus "piconesPlus")
pico_set_program_version(piconesPlus "0.3")

pico_enable_stdio_uart(piconesPlus 1)
pico_enable_stdio_usb(piconesPlus 0)

include(${CMAKE_CURRENT_LIST_DIR}/picosystem/picosystem.cmake REQUIRED)
target_link_libraries(piconesPlus PRIVATE
    picosystem
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_i2c
    hardware_interp
    hardware_timer
    hardware_clocks
    hardware_pwm
    hardware_flash
    infones
)

    target_include_directories(piconesPlus
    PRIVATE
        pico_lib
        infones
        assets
        .
    )

   
  
    add_subdirectory(infones)

    pico_add_extra_outputs(piconesPlus)



