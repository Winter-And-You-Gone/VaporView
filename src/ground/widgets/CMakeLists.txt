add_library(vaporview_ground_widgets STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/DevicePanelCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DevicePanelCoordinator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/EpsilonPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SegmentedSwitchButton.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SegmentedSwitchButton.h
    ${CMAKE_CURRENT_SOURCE_DIR}/TelemetryPanels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TelemetryPanels.h
    ${CMAKE_CURRENT_SOURCE_DIR}/TemperatureControllerPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TemperatureControllerWidgets.h
)

target_include_directories(vaporview_ground_widgets PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_widgets PUBLIC
    Qt6::Widgets
)

target_link_libraries(vaporview_ground_widgets PRIVATE
    vaporview_app_theme
    Qt6::Svg
)

if(MSVC)
    target_compile_options(vaporview_ground_widgets PRIVATE
        /W4
        /utf-8
        /Zc:__cplusplus
        /permissive-
    )
endif()
