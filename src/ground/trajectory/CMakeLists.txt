add_library(vaporview_ground_trajectory STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/TrajectoryViewerDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TrajectoryViewerDialog.h
)

target_include_directories(vaporview_ground_trajectory PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_trajectory PUBLIC
    Qt6::Widgets
)

target_link_libraries(vaporview_ground_trajectory PRIVATE
    vaporview_app_config
    vaporview_app_theme
    vaporview_ground_session_core
    vaporview_ground_ui_support
    vaporview_geo_core
    Qt6::Core
    Qt6::Network
    Qt6::Svg
)
