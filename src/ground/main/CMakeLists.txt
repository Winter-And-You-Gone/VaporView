add_library(vaporview_ground_main_support STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundDeviceSupport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundIconSupport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundMainWindowSupport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundStyleSupport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundWindowChromeSupport.cpp
)

target_include_directories(vaporview_ground_main_support PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_main_support PRIVATE
    vaporview_app_theme
    vaporview_sky_core
    Qt6::Widgets
    Qt6::Svg
)

if(MSVC)
    target_compile_options(vaporview_ground_main_support PRIVATE
        /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h
        /Zc:__cplusplus
        /permissive-
    )
endif()

if(WIN32)
    target_link_libraries(vaporview_ground_main_support PRIVATE dwmapi)
endif()
