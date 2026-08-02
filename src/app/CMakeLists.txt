set(NAV_SOURCES
    ${CMAKE_SOURCE_DIR}/src/app/ground_main.cpp
    ${CMAKE_SOURCE_DIR}/src/app/StartupSplash.cpp
    ${CMAKE_SOURCE_DIR}/src/app/StartupSplash.h
)

set(SKY_TUI_SOURCES
    ${CMAKE_SOURCE_DIR}/src/app/sky_main.cpp
)

set(SKY_TUI_HEADERS
)

set(NAV_UI_FILES
)

set(NAV_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources/vaporview_startup.qrc
    ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
    ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
    ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
    ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO/VaporViewLOGO_black.svg
    ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO/VaporViewLOGO_rgb217_119_87.svg
    ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO/VaporViewLOGO_white.svg
)

set(LUCIDE_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources/lucide/activity.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/audio-waveform.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/check.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-down.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-down-dark.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-down-primary-dark.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-down-primary.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-right.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-up.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-up-dark.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-up-primary-dark.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/chevron-up-primary.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/circle-x.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/copy.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/earth.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/flag-triangle-left.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/flag-triangle-right.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/flag.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/funnel.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/key.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/languages.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/layers-3.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/link.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/list-filter.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/maximize.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/menu.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/minus.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/moon.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/mountain-snow.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/panel-right-close.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/panel-right-open.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/pause.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/pencil-sparkles.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/pencil.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/play.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/plug.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/radar.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/refresh-cw.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/satellite.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/sliders-vertical.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/scroll-text.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/square-activity.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/square-check-big.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/square.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/sun.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/thermometer.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/timer.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/trash-2.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/triangle-alert.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/unlink.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/unplug.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/usb.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/x.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/zoom-in.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/zoom-out.svg
    ${CMAKE_SOURCE_DIR}/resources/lucide/LICENSE
)

set(NAV_WIN_RESOURCES)
if(WIN32)
    list(APPEND NAV_WIN_RESOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

if(WIN32)
    add_executable(VaporView WIN32
        ${NAV_SOURCES}
        ${NAV_UI_FILES}
        ${NAV_RESOURCES}
        ${LUCIDE_RESOURCES}
        ${NAV_WIN_RESOURCES}
    )
else()
    add_executable(VaporView
        ${NAV_SOURCES}
        ${NAV_UI_FILES}
        ${NAV_RESOURCES}
        ${LUCIDE_RESOURCES}
    )
endif()

target_include_directories(VaporView PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(VaporView PRIVATE
    Qt6::Widgets
    Qt6::Svg
    vaporview_main_window
    vaporview_app_theme
    vaporview_sky_core
)

if(VAPORVIEW_ENABLE_OSGEARTH)
    if(WIN32)
        vaporview_stage_osgearth_runtime(VaporView)
    endif()
endif()

add_executable(VaporViewSky
    ${SKY_TUI_SOURCES}
    ${SKY_TUI_HEADERS}
)

target_include_directories(VaporViewSky PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(VaporViewSky PRIVATE
    vaporview_sky_core
    vaporview_sky_tui
)

add_executable(VaporViewSkyCore
    ${CMAKE_SOURCE_DIR}/src/app/sky_core_main.cpp
)

target_include_directories(VaporViewSkyCore PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VaporViewSkyCore PRIVATE
    vaporview_sky_core
)

add_executable(VaporViewSkyTui
    ${CMAKE_SOURCE_DIR}/src/app/sky_tui_main.cpp
)

target_include_directories(VaporViewSkyTui PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VaporViewSkyTui PRIVATE
    vaporview_sky_tui
)

add_dependencies(VaporViewSkyTui VaporViewSkyCore)
add_dependencies(VaporViewSky VaporViewSkyTui)
add_dependencies(VaporView VaporViewSky)

if(WIN32)
    foreach(vaporview_app_target IN ITEMS VaporView VaporViewSky VaporViewSkyCore VaporViewSkyTui)
        vaporview_set_windows_as_invoker_manifest(${vaporview_app_target})
    endforeach()
endif()

add_custom_target(VaporViewRuntimeResources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
        ${CMAKE_BINARY_DIR}/resources/modern_style.qss
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
        ${CMAKE_BINARY_DIR}/resources/combo_arrow_down.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
        ${CMAKE_BINARY_DIR}/resources/combo_arrow_up.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/lucide
        ${CMAKE_BINARY_DIR}/resources/lucide
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO
        ${CMAKE_BINARY_DIR}/resources/VaproViewLOGO
    DEPENDS ${NAV_RESOURCES} ${LUCIDE_RESOURCES}
    COMMENT "Synchronizing VaporView runtime resources"
)
add_dependencies(VaporView VaporViewRuntimeResources)

add_custom_command(TARGET VaporView POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VaporView>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
        $<TARGET_FILE_DIR:VaporView>/resources/modern_style.qss
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
        $<TARGET_FILE_DIR:VaporView>/resources/combo_arrow_down.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
        $<TARGET_FILE_DIR:VaporView>/resources/combo_arrow_up.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/lucide
        $<TARGET_FILE_DIR:VaporView>/resources/lucide
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO
        $<TARGET_FILE_DIR:VaporView>/resources/VaproViewLOGO
    COMMENT "Copying resources to build directory"
)

add_custom_command(TARGET VaporViewSky POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VaporViewSky>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/sky_startup_logo_ansi.txt
        $<TARGET_FILE_DIR:VaporViewSky>/resources/sky_startup_logo_ansi.txt
    COMMENT "Copying VaporViewSky resources to build directory"
)

add_custom_command(TARGET VaporViewSkyTui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VaporViewSkyTui>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/sky_startup_logo_ansi.txt
        $<TARGET_FILE_DIR:VaporViewSkyTui>/resources/sky_startup_logo_ansi.txt
    COMMENT "Copying VaporViewSkyTui resources to build directory"
)

vaporview_configure_platform_runtime()
