add_subdirectory(session)
add_subdirectory(devices)

if(TARGET vaporview_map3d_osgearth)
    add_subdirectory(map)
endif()

add_library(vaporview_ground_ui_support STATIC
    ${CMAKE_SOURCE_DIR}/src/CustomTitleBar.cpp
    ${CMAKE_SOURCE_DIR}/src/GroundTelemetryService.cpp
    ${CMAKE_SOURCE_DIR}/src/SkyDeviceConfigDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/WindowSizing.cpp
    ${CMAKE_SOURCE_DIR}/include/CustomTitleBar.h
    ${CMAKE_SOURCE_DIR}/include/GroundTelemetryService.h
    ${CMAKE_SOURCE_DIR}/include/SkyDeviceConfigDialog.h
    ${CMAKE_SOURCE_DIR}/include/WindowSizing.h
)

target_include_directories(vaporview_ground_ui_support PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_ui_support PUBLIC
    vaporview_app_theme
    vaporview_sky_core
    Qt6::Widgets
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(vaporview_ground_ui_support PUBLIC dwmapi)
endif()
