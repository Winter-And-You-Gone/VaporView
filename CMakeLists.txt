cmake_minimum_required(VERSION 3.16)

project(VaporView VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(BUILD_PYTHON_BINDINGS "Build Python bindings with pybind11" OFF)
option(VAPORVIEW_ENABLE_OSGEARTH "Enable osgEarth 3D local map viewer" OFF)
set(VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR
    "${CMAKE_SOURCE_DIR}/.local_deps/vcpkg_installed"
    CACHE PATH
    "Project-local vcpkg installed directory used for optional native dependencies"
)

find_package(Qt6 COMPONENTS Core Widgets SerialPort Network Svg Concurrent REQUIRED)
set(QT_LIBRARIES Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Network Qt6::Svg Qt6::Concurrent)

if(WIN32)
    find_program(QT_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS
            "${Qt6_DIR}/../../../bin"
            ${CMAKE_PREFIX_PATH}/bin
            ENV PATH
    )
endif()

find_package(Threads REQUIRED)
include(CTest)

set(UM982_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/um982_driver)
set(HIPNUC_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/hipnuc_driver)
set(RTKLIB_DIR ${CMAKE_SOURCE_DIR}/third_party/rtklib)
set(RTKLIB_STR2STR_SOURCE ${RTKLIB_DIR}/app/consapp/str2str/str2str.c)

set(RTKLIB_SOURCES
    ${RTKLIB_DIR}/src/stream.c
    ${RTKLIB_DIR}/src/streamsvr.c
    ${RTKLIB_DIR}/src/rtkcmn.c
    ${RTKLIB_DIR}/src/solution.c
    ${RTKLIB_DIR}/src/sbas.c
    ${RTKLIB_DIR}/src/geoid.c
    ${RTKLIB_DIR}/src/rcvraw.c
    ${RTKLIB_DIR}/src/rtcm.c
    ${RTKLIB_DIR}/src/rtcm2.c
    ${RTKLIB_DIR}/src/rtcm3.c
    ${RTKLIB_DIR}/src/rtcm3e.c
    ${RTKLIB_DIR}/src/preceph.c
    ${RTKLIB_DIR}/src/rcv/novatel.c
    ${RTKLIB_DIR}/src/rcv/ublox.c
    ${RTKLIB_DIR}/src/rcv/ss2.c
    ${RTKLIB_DIR}/src/rcv/crescent.c
    ${RTKLIB_DIR}/src/rcv/skytraq.c
    ${RTKLIB_DIR}/src/rcv/javad.c
    ${RTKLIB_DIR}/src/rcv/nvs.c
    ${RTKLIB_DIR}/src/rcv/binex.c
    ${RTKLIB_DIR}/src/rcv/rt17.c
    ${RTKLIB_DIR}/src/rcv/septentrio.c
)

add_library(rtklib_strsvr STATIC ${RTKLIB_SOURCES})
target_include_directories(rtklib_strsvr PUBLIC ${RTKLIB_DIR}/src)
target_compile_definitions(rtklib_strsvr PRIVATE
    ENAGLO
    ENAGAL
    ENAQZS
    ENACMP
    ENAIRN
    NFREQ=5
    NEXOBS=3
    SVR_REUSEADDR
)

if(MSVC)
    target_compile_definitions(rtklib_strsvr PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rtklib_strsvr PRIVATE /W0 /Zc:__cplusplus /permissive-)
else()
    target_compile_options(rtklib_strsvr PRIVATE -w)
endif()

target_link_libraries(rtklib_strsvr PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(rtklib_strsvr PUBLIC ws2_32 winmm)
elseif(UNIX)
    target_link_libraries(rtklib_strsvr PUBLIC m)
endif()

if(EXISTS ${RTKLIB_STR2STR_SOURCE})
    add_executable(str2str ${RTKLIB_STR2STR_SOURCE})
    target_include_directories(str2str PRIVATE ${RTKLIB_DIR}/src)
    target_link_libraries(str2str PRIVATE rtklib_strsvr)

    if(MSVC)
        target_compile_definitions(str2str PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(str2str PRIVATE /W0 /utf-8 /Zc:__cplusplus /permissive-)
    else()
        target_compile_options(str2str PRIVATE -w)
    endif()
endif()

set(PROTOCOL_SOURCES
    src/TelemetryCodec.cpp
    src/TelemetryLink.cpp
    src/SkyConfig.cpp
)

set(PROTOCOL_HEADERS
    include/TelemetryTypes.h
    include/TelemetryCodec.h
    include/TelemetryLink.h
    include/SkyConfig.h
)

add_library(vaporview_protocol STATIC
    ${PROTOCOL_SOURCES}
    ${PROTOCOL_HEADERS}
)

target_include_directories(vaporview_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(vaporview_protocol PUBLIC
    Qt6::Core
)

add_library(vaporview_app_theme STATIC
    src/AppTheme.cpp
    include/AppTheme.h
)

target_include_directories(vaporview_app_theme PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(vaporview_app_theme PUBLIC
    Qt6::Widgets
)

set(GEO_CORE_SOURCES
    src/geo/CoordinateTransform.cpp
    src/geo/TrajectoryBuffer.cpp
    src/geo/TrajectoryReplay.cpp
    src/geo/TrajectoryQuality.cpp
    src/geo/SessionTrackReader.cpp
)

set(GEO_CORE_HEADERS
    include/geo/CoordinateTransform.h
    include/geo/GeoTypes.h
    include/geo/TrajectoryBuffer.h
    include/geo/TrajectoryReplay.h
    include/geo/TrajectoryQuality.h
    include/geo/SessionTrackReader.h
)

add_library(vaporview_geo_core STATIC
    ${GEO_CORE_SOURCES}
    ${GEO_CORE_HEADERS}
)

target_include_directories(vaporview_geo_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(vaporview_geo_core PUBLIC
    Qt6::Core
)

if(VAPORVIEW_ENABLE_OSGEARTH)
    find_package(Qt6 COMPONENTS OpenGLWidgets REQUIRED)
    if(EXISTS "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows")
        list(PREPEND CMAKE_PREFIX_PATH "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows")
    endif()
    find_package(osgEarth CONFIG REQUIRED)
    find_package(OpenSceneGraph REQUIRED COMPONENTS osg osgDB osgGA osgViewer osgUtil)

    if(TARGET osgEarth::osgEarth)
        set(OSGEARTH_TARGET osgEarth::osgEarth)
    elseif(TARGET osgEarth)
        set(OSGEARTH_TARGET osgEarth)
    else()
        message(FATAL_ERROR "osgEarth package was found but no known osgEarth target is available")
    endif()

    set(MAP3D_SOURCES
        src/map3d/MapDataManager.cpp
        src/map3d/Map3DWindow.cpp
        src/map3d/OsgEarthViewWidget.cpp
        src/map3d/Trajectory3DLayer.cpp
        src/map3d/Aircraft3DLayer.cpp
    )

    set(MAP3D_HEADERS
        include/map3d/MapDataManager.h
        include/map3d/Map3DWindow.h
        include/map3d/OsgEarthViewWidget.h
        include/map3d/Trajectory3DLayer.h
        include/map3d/Aircraft3DLayer.h
    )

    add_library(vaporview_map3d_osgearth STATIC
        ${MAP3D_SOURCES}
        ${MAP3D_HEADERS}
    )

    target_include_directories(vaporview_map3d_osgearth
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include
        PRIVATE
            ${OPENSCENEGRAPH_INCLUDE_DIRS}
    )

    target_compile_definitions(vaporview_map3d_osgearth
        PUBLIC
            VAPORVIEW_HAS_OSGEARTH=1
    )

    target_link_libraries(vaporview_map3d_osgearth
        PUBLIC
            vaporview_geo_core
            Qt6::Widgets
            Qt6::OpenGLWidgets
            ${OSGEARTH_TARGET}
            ${OPENSCENEGRAPH_LIBRARIES}
    )

    if(WIN32)
        set(VAPORVIEW_OSGEARTH_RUNTIME_BIN_DIR
            "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/bin"
            CACHE PATH
            "Runtime DLL directory for the optional project-local osgEarth install"
        )
        set(VAPORVIEW_OSGEARTH_PLUGIN_DIR
            "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/plugins"
            CACHE PATH
            "OSG plugin directory for the optional project-local osgEarth install"
        )

        set(vaporview_osgearth_runtime_commands)
        if(EXISTS "${VAPORVIEW_OSGEARTH_RUNTIME_BIN_DIR}")
            list(APPEND vaporview_osgearth_runtime_commands
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${VAPORVIEW_OSGEARTH_RUNTIME_BIN_DIR}"
                    "${CMAKE_CURRENT_BINARY_DIR}"
            )
        endif()

        if(EXISTS "${VAPORVIEW_OSGEARTH_PLUGIN_DIR}")
            file(GLOB vaporview_osg_plugin_dirs
                LIST_DIRECTORIES true
                "${VAPORVIEW_OSGEARTH_PLUGIN_DIR}/osgPlugins-*"
            )
            foreach(vaporview_osg_plugin_dir IN LISTS vaporview_osg_plugin_dirs)
                get_filename_component(vaporview_osg_plugin_name
                    "${vaporview_osg_plugin_dir}"
                    NAME
                )
                list(APPEND vaporview_osgearth_runtime_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${vaporview_osg_plugin_dir}"
                        "${CMAKE_CURRENT_BINARY_DIR}/${vaporview_osg_plugin_name}"
                )
            endforeach()
        endif()

        foreach(vaporview_osgearth_share_dir IN ITEMS gdal proj proj4)
            if(EXISTS "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/share/${vaporview_osgearth_share_dir}")
                list(APPEND vaporview_osgearth_runtime_commands
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${CMAKE_CURRENT_BINARY_DIR}/share/${vaporview_osgearth_share_dir}"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/share/${vaporview_osgearth_share_dir}"
                        "${CMAKE_CURRENT_BINARY_DIR}/share/${vaporview_osgearth_share_dir}"
                )
            endif()
        endforeach()

        set(vaporview_default_earth_file "${CMAKE_SOURCE_DIR}/data/maps/vaporview_default.earth")
        set(vaporview_dem_earth_file "${CMAKE_SOURCE_DIR}/data/maps/vaporview_with_dem.earth")
        set(vaporview_srtm_earth_file "${CMAKE_SOURCE_DIR}/data/maps/vaporview_with_srtm.earth")
        set(vaporview_full_local_earth_file "${CMAKE_SOURCE_DIR}/data/maps/vaporview_full_local.earth")
        set(vaporview_sentinel2_imagery_earth_file "${CMAKE_SOURCE_DIR}/data/maps/vaporview_with_sentinel2_imagery.earth")
        set(vaporview_landsat_imagery_earth_file "${CMAKE_SOURCE_DIR}/data/maps/vaporview_with_landsat_imagery.earth")
        set(vaporview_openaerialmap_imagery_earth_file "${CMAKE_SOURCE_DIR}/data/maps/vaporview_with_openaerialmap_imagery.earth")
        set(vaporview_natural_earth_texture
            "${CMAKE_SOURCE_DIR}/data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"
        )
        set(vaporview_natural_earth_vrt
            "${CMAKE_SOURCE_DIR}/data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"
        )
        set(vaporview_natural_earth_raster
            "${CMAKE_SOURCE_DIR}/data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"
        )
        if(EXISTS "${vaporview_default_earth_file}" AND EXISTS "${vaporview_natural_earth_vrt}" AND EXISTS "${vaporview_natural_earth_raster}")
            list(APPEND vaporview_osgearth_runtime_commands
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/natural_earth/NE2_50M_SR_W"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_default_earth_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/vaporview_default.earth"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_dem_earth_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/vaporview_with_dem.earth"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_srtm_earth_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/vaporview_with_srtm.earth"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_full_local_earth_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/vaporview_full_local.earth"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_sentinel2_imagery_earth_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/vaporview_with_sentinel2_imagery.earth"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_landsat_imagery_earth_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/vaporview_with_landsat_imagery.earth"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_openaerialmap_imagery_earth_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/vaporview_with_openaerialmap_imagery.earth"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_natural_earth_vrt}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_natural_earth_raster}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${vaporview_natural_earth_texture}"
                    "${CMAKE_CURRENT_BINARY_DIR}/data/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"
            )
        endif()

        if(vaporview_osgearth_runtime_commands)
            add_custom_target(VaporViewOsgEarthRuntime
                ${vaporview_osgearth_runtime_commands}
                COMMENT "Synchronizing project-local osgEarth runtime"
            )

            function(vaporview_stage_osgearth_runtime target_name)
                add_dependencies(${target_name} VaporViewOsgEarthRuntime)
            endfunction()
        else()
            function(vaporview_stage_osgearth_runtime target_name)
                message(WARNING
                    "No project-local osgEarth runtime directories found for ${target_name}; "
                    "runtime DLLs must be available on PATH."
                )
            endfunction()
        endif()
    endif()
endif()

set(SKY_CORE_SOURCES
    src/SkyRuntime.cpp
    src/SkyDeviceManager.cpp
    src/SkySessionRecorder.cpp
    src/SerialTelemetryLink.cpp
    src/TcpTelemetryLink.cpp
    src/SkyLocalIpcServer.cpp
    src/TcpWaveEncoding.cpp
    src/TemperatureControllerProtocol.cpp
    src/serial_port.cpp
    src/data_collector.cpp
    ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
)

set(SKY_CORE_HEADERS
    include/SkyRuntime.h
    include/SkyDashboardTypes.h
    include/SkyDeviceManager.h
    include/SkySessionRecorder.h
    include/SerialTelemetryLink.h
    include/TcpTelemetryLink.h
    include/SkyLocalIpcServer.h
    include/TcpWaveEncoding.h
    include/TemperatureControllerProtocol.h
    include/serial_port.h
    include/data_types.h
    include/data_collector.h
    ${UM982_DRIVER_DIR}/pvtsln_data.hpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.h
)

add_library(vaporview_sky_core STATIC
    ${SKY_CORE_SOURCES}
    ${SKY_CORE_HEADERS}
)

target_include_directories(vaporview_sky_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(vaporview_sky_core PUBLIC
    vaporview_protocol
    Qt6::Core
    Qt6::SerialPort
    Qt6::Network
    Threads::Threads
)

set(SKY_TUI_COMMON_SOURCES
    src/SkyTuiApp.cpp
    src/SkyTuiController.cpp
    src/SkyTuiTheme.cpp
    src/SkyStartupScreen.cpp
    src/SkyLocalIpcClient.cpp
)

set(SKY_TUI_COMMON_HEADERS
    include/SkyTuiApp.h
    include/SkyTuiController.h
    include/SkyTuiModel.h
    include/SkyTuiTheme.h
    include/SkyTuiOptions.h
    include/SkyStartupScreen.h
    include/SkyLocalIpcClient.h
)

add_library(vaporview_sky_tui STATIC
    ${SKY_TUI_COMMON_SOURCES}
    ${SKY_TUI_COMMON_HEADERS}
)

target_include_directories(vaporview_sky_tui PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_sky_tui PUBLIC
    vaporview_app_theme
    vaporview_protocol
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
)

set(NAV_WINDOW_SOURCES
    src/MainWindow.cpp
    src/GroundTelemetryService.cpp
    src/SkyDeviceConfigDialog.cpp
    src/TcpWavePanel.cpp
    src/RawDataParserWindow.cpp
    src/SessionViewerWindow.cpp
    src/SessionTimeFormat.cpp
    src/TrajectoryViewerDialog.cpp
    src/RtkConfigDialog.cpp
    src/RtkStreamService.cpp
    src/CustomTitleBar.cpp
    src/WindowSizing.cpp
)

set(NAV_SOURCES
    src/main.cpp
    ${NAV_WINDOW_SOURCES}
)

set(NAV_HEADERS
    include/AppTheme.h
    include/MainWindow.h
    include/GroundTelemetryService.h
    include/SkyDeviceConfigDialog.h
    include/TcpWavePanel.h
    include/RawDataParserWindow.h
    include/SessionViewerWindow.h
    include/SessionTimeFormat.h
    include/TrajectoryViewerDialog.h
    include/RtkConfigDialog.h
    include/RtkStreamService.h
    include/CustomTitleBar.h
    include/VisualTextLabel.h
    include/WindowSizing.h
)

set(SKY_TUI_SOURCES
    src/sky_main.cpp
)

set(SKY_TUI_HEADERS
)

set(NAV_UI_FILES
)

set(NAV_RESOURCES
    resources/modern_style.qss
    resources/combo_arrow_down.xpm
    resources/combo_arrow_up.xpm
    resources/VaproViewLOGO/VaporViewLOGO_black.svg
    resources/VaproViewLOGO/VaporViewLOGO_rgb217_119_87.svg
    resources/VaproViewLOGO/VaporViewLOGO_white.svg
)

set(LUCIDE_RESOURCES
    resources/lucide/activity.svg
    resources/lucide/audio-waveform.svg
    resources/lucide/check.svg
    resources/lucide/chevron-down.svg
    resources/lucide/circle-x.svg
    resources/lucide/copy.svg
    resources/lucide/earth.svg
    resources/lucide/flag-triangle-left.svg
    resources/lucide/flag-triangle-right.svg
    resources/lucide/flag.svg
    resources/lucide/funnel.svg
    resources/lucide/key.svg
    resources/lucide/languages.svg
    resources/lucide/link.svg
    resources/lucide/list-filter.svg
    resources/lucide/maximize.svg
    resources/lucide/menu.svg
    resources/lucide/minus.svg
    resources/lucide/moon.svg
    resources/lucide/mountain-snow.svg
    resources/lucide/panel-right-close.svg
    resources/lucide/panel-right-open.svg
    resources/lucide/pause.svg
    resources/lucide/pencil-sparkles.svg
    resources/lucide/pencil.svg
    resources/lucide/play.svg
    resources/lucide/plug.svg
    resources/lucide/refresh-cw.svg
    resources/lucide/satellite.svg
    resources/lucide/sliders-vertical.svg
    resources/lucide/scroll-text.svg
    resources/lucide/square-activity.svg
    resources/lucide/square-check-big.svg
    resources/lucide/square.svg
    resources/lucide/sun.svg
    resources/lucide/thermometer.svg
    resources/lucide/timer.svg
    resources/lucide/trash-2.svg
    resources/lucide/triangle-alert.svg
    resources/lucide/unlink.svg
    resources/lucide/unplug.svg
    resources/lucide/usb.svg
    resources/lucide/x.svg
    resources/lucide/zoom-in.svg
    resources/lucide/zoom-out.svg
    resources/lucide/LICENSE
)

set(NAV_WIN_RESOURCES)
if(WIN32)
    list(APPEND NAV_WIN_RESOURCES resources/app.rc)
endif()

if(WIN32)
    add_executable(VaporView WIN32
        ${NAV_SOURCES}
        ${NAV_HEADERS}
        ${NAV_UI_FILES}
        ${NAV_RESOURCES}
        ${LUCIDE_RESOURCES}
        ${NAV_WIN_RESOURCES}
    )
else()
    add_executable(VaporView
        ${NAV_SOURCES}
        ${NAV_HEADERS}
        ${NAV_UI_FILES}
        ${NAV_RESOURCES}
        ${LUCIDE_RESOURCES}
    )
endif()

add_library(vaporview_main_window STATIC
    ${NAV_WINDOW_SOURCES}
    ${NAV_HEADERS}
    ${NAV_UI_FILES}
    ${NAV_RESOURCES}
    ${LUCIDE_RESOURCES}
)

if(BUILD_TESTING)
    target_compile_definitions(vaporview_main_window PRIVATE
        VAPORVIEW_MAIN_WINDOW_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(VaporView PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_main_window PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /Zc:__cplusplus /permissive-)
endif()

target_include_directories(VaporView PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_include_directories(vaporview_main_window PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(VaporView PRIVATE
    ${QT_LIBRARIES}
    vaporview_app_theme
    vaporview_geo_core
    vaporview_sky_core
    rtklib_strsvr
)

target_link_libraries(vaporview_main_window PUBLIC
    ${QT_LIBRARIES}
    vaporview_app_theme
    vaporview_geo_core
    vaporview_sky_core
    rtklib_strsvr
)

if(VAPORVIEW_ENABLE_OSGEARTH)
    target_link_libraries(VaporView PRIVATE vaporview_map3d_osgearth)
    target_compile_definitions(VaporView PRIVATE VAPORVIEW_HAS_OSGEARTH=1)
    target_link_libraries(vaporview_main_window PUBLIC vaporview_map3d_osgearth)
    target_compile_definitions(vaporview_main_window PUBLIC VAPORVIEW_HAS_OSGEARTH=1)
    if(WIN32)
        vaporview_stage_osgearth_runtime(VaporView)
    endif()
endif()

if(WIN32)
    target_link_libraries(VaporView PRIVATE dwmapi)
    target_link_libraries(vaporview_main_window PUBLIC dwmapi)
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
    src/sky_core_main.cpp
)

target_include_directories(VaporViewSkyCore PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VaporViewSkyCore PRIVATE
    vaporview_sky_core
)

add_executable(VaporViewSkyTui
    src/sky_tui_main.cpp
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

add_custom_target(VaporViewRuntimeResources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
        ${CMAKE_CURRENT_BINARY_DIR}/resources/modern_style.qss
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
        ${CMAKE_CURRENT_BINARY_DIR}/resources/combo_arrow_down.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
        ${CMAKE_CURRENT_BINARY_DIR}/resources/combo_arrow_up.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/lucide
        ${CMAKE_CURRENT_BINARY_DIR}/resources/lucide
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO
        ${CMAKE_CURRENT_BINARY_DIR}/resources/VaproViewLOGO
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

if(BUILD_TESTING)
    add_executable(trajectory_buffer_test
        tests/trajectory_buffer_test.cpp
    )
    target_link_libraries(trajectory_buffer_test PRIVATE vaporview_geo_core)
    add_test(NAME trajectory_buffer_test COMMAND trajectory_buffer_test)

    add_executable(trajectory_quality_test
        tests/trajectory_quality_test.cpp
    )
    target_link_libraries(trajectory_quality_test PRIVATE vaporview_geo_core)
    add_test(NAME trajectory_quality_test COMMAND trajectory_quality_test)

    add_executable(trajectory_replay_test
        tests/trajectory_replay_test.cpp
    )
    target_link_libraries(trajectory_replay_test PRIVATE vaporview_geo_core)
    add_test(NAME trajectory_replay_test COMMAND trajectory_replay_test)

    add_executable(session_track_reader_test
        tests/session_track_reader_test.cpp
    )
    target_link_libraries(session_track_reader_test PRIVATE vaporview_geo_core)
    add_test(NAME session_track_reader_test COMMAND session_track_reader_test)

    add_executable(coordinate_transform_test
        tests/coordinate_transform_test.cpp
    )
    target_link_libraries(coordinate_transform_test PRIVATE vaporview_geo_core)
    add_test(NAME coordinate_transform_test COMMAND coordinate_transform_test)

    add_executable(height_reference_test
        tests/height_reference_test.cpp
    )
    target_link_libraries(height_reference_test PRIVATE vaporview_geo_core)
    add_test(NAME height_reference_test COMMAND height_reference_test)

    add_executable(map_data_script_test
        tests/map_data_script_test.cpp
    )
    target_compile_definitions(map_data_script_test PRIVATE
        VAPORVIEW_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(map_data_script_test PRIVATE Qt6::Core)
    add_test(NAME map_data_script_test COMMAND map_data_script_test)

    add_executable(tcp_wave_encoding_test
        tests/tcp_wave_encoding_test.cpp
    )
    target_include_directories(tcp_wave_encoding_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(tcp_wave_encoding_test PRIVATE vaporview_sky_core)
    add_test(NAME tcp_wave_encoding_test COMMAND tcp_wave_encoding_test)

    add_executable(temperature_controller_protocol_test
        tests/temperature_controller_protocol_test.cpp
    )
    target_include_directories(temperature_controller_protocol_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(temperature_controller_protocol_test PRIVATE vaporview_sky_core)
    add_test(NAME temperature_controller_protocol_test COMMAND temperature_controller_protocol_test)

    add_executable(telemetry_codec_test
        tests/telemetry_codec_test.cpp
    )
    target_include_directories(telemetry_codec_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(telemetry_codec_test PRIVATE vaporview_protocol)
    add_test(NAME telemetry_codec_test COMMAND telemetry_codec_test)

    add_executable(telemetry_tcp_link_test
        tests/telemetry_tcp_link_test.cpp
    )
    target_include_directories(telemetry_tcp_link_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(telemetry_tcp_link_test PRIVATE vaporview_sky_core)
    add_test(NAME telemetry_tcp_link_test COMMAND telemetry_tcp_link_test)

    add_executable(sky_device_config_dialog_test
        tests/sky_device_config_dialog_test.cpp
        src/SkyDeviceConfigDialog.cpp
        src/CustomTitleBar.cpp
        src/WindowSizing.cpp
        src/GroundTelemetryService.cpp
        include/SkyDeviceConfigDialog.h
        include/GroundTelemetryService.h
    )
    target_include_directories(sky_device_config_dialog_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(sky_device_config_dialog_test PRIVATE
        vaporview_app_theme
        vaporview_sky_core
        Qt6::Widgets
        Qt6::Svg
    )
    if(WIN32)
        target_link_libraries(sky_device_config_dialog_test PRIVATE dwmapi)
    endif()
    add_test(NAME sky_device_config_dialog_test COMMAND sky_device_config_dialog_test)

    add_executable(sky_session_recorder_test
        tests/sky_session_recorder_test.cpp
    )
    target_include_directories(sky_session_recorder_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(sky_session_recorder_test PRIVATE vaporview_sky_core)
    add_test(NAME sky_session_recorder_test COMMAND sky_session_recorder_test)

    add_executable(sky_local_ipc_client_test
        tests/sky_local_ipc_client_test.cpp
    )
    target_include_directories(sky_local_ipc_client_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(sky_local_ipc_client_test PRIVATE vaporview_sky_tui)
    add_test(NAME sky_local_ipc_client_test COMMAND sky_local_ipc_client_test)

    add_executable(visual_text_label_test
        tests/visual_text_label_test.cpp
    )
    target_include_directories(visual_text_label_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(visual_text_label_test PRIVATE Qt6::Widgets)
    add_test(NAME visual_text_label_test COMMAND visual_text_label_test)

    add_executable(session_time_format_test
        tests/session_time_format_test.cpp
        src/SessionTimeFormat.cpp
    )
    target_include_directories(session_time_format_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(session_time_format_test PRIVATE Qt6::Core)
    add_test(NAME session_time_format_test COMMAND session_time_format_test)

    add_executable(main_window_layout_test
        tests/main_window_layout_test.cpp
    )
    target_include_directories(main_window_layout_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(main_window_layout_test PRIVATE vaporview_main_window)
    add_dependencies(main_window_layout_test VaporViewRuntimeResources)
    if(VAPORVIEW_ENABLE_OSGEARTH AND WIN32)
        vaporview_stage_osgearth_runtime(main_window_layout_test)
    endif()
    add_custom_command(TARGET main_window_layout_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:main_window_layout_test>/resources
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
            $<TARGET_FILE_DIR:main_window_layout_test>/resources/modern_style.qss
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
            $<TARGET_FILE_DIR:main_window_layout_test>/resources/combo_arrow_down.xpm
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
            $<TARGET_FILE_DIR:main_window_layout_test>/resources/combo_arrow_up.xpm
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/lucide
            $<TARGET_FILE_DIR:main_window_layout_test>/resources/lucide
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO
            $<TARGET_FILE_DIR:main_window_layout_test>/resources/VaproViewLOGO
        COMMENT "Copying resources for main_window_layout_test"
    )
    add_test(NAME main_window_layout_test COMMAND main_window_layout_test)

    add_executable(session_viewer_theme_test
        tests/session_viewer_theme_test.cpp
    )
    target_include_directories(session_viewer_theme_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(session_viewer_theme_test PRIVATE vaporview_main_window)
    if(VAPORVIEW_ENABLE_OSGEARTH AND WIN32)
        vaporview_stage_osgearth_runtime(session_viewer_theme_test)
    endif()
    add_test(NAME session_viewer_theme_test COMMAND session_viewer_theme_test)

    if(VAPORVIEW_ENABLE_OSGEARTH)
        add_executable(map_data_manager_test
            tests/map_data_manager_test.cpp
        )
        target_include_directories(map_data_manager_test PRIVATE
            ${CMAKE_SOURCE_DIR}/include
        )
        target_link_libraries(map_data_manager_test PRIVATE vaporview_map3d_osgearth)
        add_test(NAME map_data_manager_test COMMAND map_data_manager_test)

        add_executable(trajectory_3d_layer_test
            tests/trajectory_3d_layer_test.cpp
        )
        target_include_directories(trajectory_3d_layer_test PRIVATE
            ${CMAKE_SOURCE_DIR}/include
        )
        target_link_libraries(trajectory_3d_layer_test PRIVATE vaporview_map3d_osgearth)
        add_test(NAME trajectory_3d_layer_test COMMAND trajectory_3d_layer_test)

        add_executable(aircraft_3d_layer_test
            tests/aircraft_3d_layer_test.cpp
        )
        target_include_directories(aircraft_3d_layer_test PRIVATE
            ${CMAKE_SOURCE_DIR}/include
        )
        target_link_libraries(aircraft_3d_layer_test PRIVATE vaporview_map3d_osgearth)
        add_test(NAME aircraft_3d_layer_test COMMAND aircraft_3d_layer_test)

        add_executable(map3d_window_smoke_test
            tests/map3d_window_smoke_test.cpp
        )
        target_include_directories(map3d_window_smoke_test PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
        )
        target_link_libraries(map3d_window_smoke_test PRIVATE vaporview_map3d_osgearth)
        if(WIN32)
            vaporview_stage_osgearth_runtime(map3d_window_smoke_test)
        endif()
        add_test(NAME map3d_window_smoke_test COMMAND map3d_window_smoke_test)

        add_executable(main_window_map3d_live_test
            tests/main_window_map3d_live_test.cpp
        )
        target_include_directories(main_window_map3d_live_test PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
        )
        target_compile_definitions(main_window_map3d_live_test PRIVATE
            VAPORVIEW_MAIN_WINDOW_TESTING=1
        )
        target_link_libraries(main_window_map3d_live_test PRIVATE vaporview_main_window)
        add_dependencies(main_window_map3d_live_test VaporViewRuntimeResources)
        if(WIN32)
            vaporview_stage_osgearth_runtime(main_window_map3d_live_test)
        endif()
        add_test(NAME main_window_map3d_live_test COMMAND main_window_map3d_live_test)
    endif()

    add_executable(vaporview_startup_test
        tests/vaporview_startup_test.cpp
    )
    target_link_libraries(vaporview_startup_test PRIVATE Qt6::Core)
    add_test(NAME vaporview_startup_test COMMAND vaporview_startup_test)
endif()

if(WIN32 AND QT_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET VaporView POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporView>
            $<TARGET_FILE:VaporView>
        COMMENT "Deploying Qt runtime for VaporView"
    )
    add_custom_command(TARGET VaporViewSky POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporViewSky>
            $<TARGET_FILE:VaporViewSky>
        COMMENT "Deploying Qt runtime for VaporViewSky"
    )
    add_custom_command(TARGET VaporViewSkyCore POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporViewSkyCore>
            $<TARGET_FILE:VaporViewSkyCore>
        COMMENT "Deploying Qt runtime for VaporViewSkyCore"
    )
    add_custom_command(TARGET VaporViewSkyTui POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporViewSkyTui>
            $<TARGET_FILE:VaporViewSkyTui>
        COMMENT "Deploying Qt runtime for VaporViewSkyTui"
    )
endif()

if(WIN32)
    add_custom_command(TARGET VaporView POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporView>
            -D TARGET_FILE=$<TARGET_FILE:VaporView>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporView release runtime"
    )
    add_custom_command(TARGET VaporViewSky POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporViewSky>
            -D TARGET_FILE=$<TARGET_FILE:VaporViewSky>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporViewSky release runtime"
    )
    add_custom_command(TARGET VaporViewSkyCore POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporViewSkyCore>
            -D TARGET_FILE=$<TARGET_FILE:VaporViewSkyCore>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporViewSkyCore release runtime"
    )
    add_custom_command(TARGET VaporViewSkyTui POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporViewSkyTui>
            -D TARGET_FILE=$<TARGET_FILE:VaporViewSkyTui>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporViewSkyTui release runtime"
    )
endif()

if(MSVC)
    target_compile_options(vaporview_protocol PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_app_theme PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_sky_core PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_sky_tui PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporView PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_main_window PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporViewSky PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporViewSkyCore PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporViewSkyTui PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
else()
    target_compile_options(vaporview_protocol PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(vaporview_app_theme PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(vaporview_sky_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(vaporview_sky_tui PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporView PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporViewSky PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporViewSkyCore PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporViewSkyTui PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(BUILD_PYTHON_BINDINGS)
    set(PYTHON_BINDINGS_ENTRY ${CMAKE_SOURCE_DIR}/python/bindings.cpp)
    if(NOT EXISTS ${PYTHON_BINDINGS_ENTRY})
        message(FATAL_ERROR
            "BUILD_PYTHON_BINDINGS=ON is not supported in this repository because "
            "${PYTHON_BINDINGS_ENTRY} is missing. The Python bindings target is explicitly disabled "
            "until a real bindings entrypoint is added.")
    endif()

    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(VaporView_bindings
        ${PYTHON_BINDINGS_ENTRY}
        src/serial_port.cpp
        src/data_collector.cpp
        ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
        ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
    )

    target_include_directories(VaporView_bindings PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${UM982_DRIVER_DIR}
        ${HIPNUC_DRIVER_DIR}
    )

    if(MSVC)
        target_compile_options(VaporView_bindings PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /Zc:__cplusplus /permissive-)
    endif()

    target_link_libraries(VaporView_bindings PRIVATE
        Threads::Threads
    )
endif()

install(TARGETS VaporView VaporViewSky VaporViewSkyCore VaporViewSkyTui
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

install(FILES ${NAV_RESOURCES}
    DESTINATION resources
)

install(DIRECTORY resources/lucide
    DESTINATION resources
)

