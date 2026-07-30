add_library(vaporview_ground_rtk STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/RtkConfigDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RtkConfigDialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RtkStreamService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RtkStreamService.h
)

target_include_directories(vaporview_ground_rtk PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
)

target_link_libraries(vaporview_ground_rtk PUBLIC
    Qt6::Widgets
)

target_link_libraries(vaporview_ground_rtk PRIVATE
    vaporview_logging
    vaporview_app_theme
    vaporview_ground_ui_support
    vaporview_sky_core
    rtklib_strsvr
    Qt6::Core
    Qt6::Network
    Qt6::SerialPort
    Qt6::Svg
)
