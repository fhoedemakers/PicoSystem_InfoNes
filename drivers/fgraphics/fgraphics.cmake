if (NOT TARGET fgraphics)
    add_library(fgraphics INTERFACE)

    target_sources(fgraphics INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/fgraphics.c
    )

    target_link_libraries(fgraphics INTERFACE pico_stdlib hardware_clocks hardware_spi hardware_pio)
    target_include_directories(fgraphics INTERFACE ${CMAKE_CURRENT_LIST_DIR})
endif()
