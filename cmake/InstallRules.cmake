foreach(vaporview_product IN ITEMS VaporView VaporViewSky VaporViewSkyCore VaporViewSkyTui)
    if(TARGET ${vaporview_product})
        install(TARGETS ${vaporview_product} RUNTIME DESTINATION bin)
    endif()
endforeach()

install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
    DESTINATION include
)

install(FILES
    "${CMAKE_SOURCE_DIR}/resources/modern_style.qss"
    "${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm"
    "${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm"
    "${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO/VaporViewLOGO_black.svg"
    "${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO/VaporViewLOGO_rgb217_119_87.svg"
    "${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO/VaporViewLOGO_white.svg"
    DESTINATION resources
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/lucide"
    DESTINATION resources
)
