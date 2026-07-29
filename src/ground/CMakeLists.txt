add_subdirectory(devices)
add_subdirectory(widgets)
add_subdirectory(main)

if(TARGET vaporview_map3d_osgearth)
    add_subdirectory(map)
endif()

add_library(vaporview_ground_ui_support STATIC
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/CustomTitleBar.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/SkyDeviceConfigDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/WindowSizing.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/CustomTitleBar.h
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/SkyDeviceConfigDialog.h
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/WindowSizing.h
)

target_include_directories(vaporview_ground_ui_support PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_ui_support PUBLIC
    Qt6::Widgets
)

target_link_libraries(vaporview_ground_ui_support PRIVATE
    vaporview_app_theme
    vaporview_ground_device_core
    vaporview_sky_core
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(vaporview_ground_ui_support PRIVATE dwmapi)
endif()

add_subdirectory(rtk)
add_subdirectory(trajectory)
add_subdirectory(wave)
add_subdirectory(session)

set(NAV_WINDOW_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ground/main/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowAppearance.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowChrome.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowEpsilon.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowRates.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowRecording.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowRemoteDevices.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowSetup.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowState.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowState.h
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowTelemetry.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowUiTestMode.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/main/GroundMainWindowImplementation.h
)

set(NAV_HEADERS
    ${CMAKE_SOURCE_DIR}/src/shared/theme/AppTheme.h
    ${CMAKE_SOURCE_DIR}/src/shared/theme/SingleLevelPopupMenu.h
    ${CMAKE_SOURCE_DIR}/src/ground/main/MainWindow.h
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/SkyDeviceConfigDialog.h
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/CustomTitleBar.h
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/VisualTextLabel.h
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/WindowSizing.h
    ${CMAKE_SOURCE_DIR}/include/ground/SessionData.h
)

add_library(vaporview_main_window STATIC
    ${NAV_WINDOW_SOURCES}
    ${NAV_HEADERS}
)

if(BUILD_TESTING)
    target_compile_definitions(vaporview_main_window PRIVATE
        VAPORVIEW_MAIN_WINDOW_TESTING=1
    )
endif()

target_include_directories(vaporview_main_window PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_include_directories(vaporview_main_window PRIVATE
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(vaporview_main_window PUBLIC
    Qt6::Widgets
)

target_link_libraries(vaporview_main_window PRIVATE
    vaporview_app_theme
    vaporview_geo_core
    vaporview_sky_core
    vaporview_ground_ui_support
    rtklib_strsvr
    vaporview_ground_device_core
    vaporview_ground_temperature_command_state
    vaporview_ground_main_support
    vaporview_ground_rtk
    vaporview_ground_session_core
    vaporview_ground_session_ui
    vaporview_ground_trajectory
    vaporview_ground_wave
    vaporview_ground_widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::SerialPort
    Qt6::Svg
)

if(VAPORVIEW_ENABLE_OSGEARTH)
    target_link_libraries(vaporview_main_window PRIVATE
        vaporview_ground_map
        vaporview_map3d_osgearth
    )
    target_compile_definitions(vaporview_main_window PUBLIC VAPORVIEW_HAS_OSGEARTH=1)
endif()

if(WIN32)
    target_link_libraries(vaporview_main_window PRIVATE dwmapi)
endif()
