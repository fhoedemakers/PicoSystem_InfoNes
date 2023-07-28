# pico-infonesPlus 
cmake_minimum_required(VERSION 3.13)


set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


add_definitions( -DNDEBUG -DSPEAKER_ENABLED)
include(pico_sdk_import.cmake)

project(PicoSystem_InfoNes C CXX ASM)
pico_sdk_init()


add_executable(PicoSystem_InfoNes
    main.cpp
    tar.cpp
    menu.cpp
    RomLister.cpp
    assets/builtinrom.c
)

pico_set_program_name(PicoSystem_InfoNes "PicoSystem_InfoNes")
pico_set_program_version(PicoSystem_InfoNes "v0.8-alpha")

pico_enable_stdio_uart(PicoSystem_InfoNes 0)
pico_enable_stdio_usb(PicoSystem_InfoNes 0)

include(${CMAKE_CURRENT_LIST_DIR}/picosystem/picosystem.cmake REQUIRED)
pico_set_linker_script(PicoSystem_InfoNes ${PICOSYSTEM_LINKER_SCRIPT})
message("Linker Script    : ${PICOSYSTEM_LINKER_SCRIPT}")
target_link_libraries(PicoSystem_InfoNes PRIVATE
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

    target_include_directories(PicoSystem_InfoNes
    PRIVATE
        pico_lib
        infones
        assets
        .
    )

   
  
    add_subdirectory(infones)

    pico_add_extra_outputs(PicoSystem_InfoNes)



