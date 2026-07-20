add_library(vaporview_ground_wave STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/RawDataParserWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RawDataParserWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/TcpWavePanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TcpWavePanel.h
)

target_include_directories(vaporview_ground_wave PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

if(BUILD_TESTING)
    target_compile_definitions(vaporview_ground_wave PRIVATE
        VAPORVIEW_MAIN_WINDOW_TESTING=1
    )
endif()

target_link_libraries(vaporview_ground_wave PUBLIC
    Qt6::Widgets
)

target_link_libraries(vaporview_ground_wave PRIVATE
    vaporview_app_theme
    vaporview_ground_ui_support
    vaporview_protocol
    vaporview_session_format
    vaporview_wave_encoding
    Qt6::Concurrent
    Qt6::Network
    Qt6::Svg
)
