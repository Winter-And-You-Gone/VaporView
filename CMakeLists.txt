cmake_minimum_required(VERSION 3.16)

project(VaporView VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(BUILD_PYTHON_BINDINGS "Build Python bindings with pybind11" OFF)

find_package(Qt6 COMPONENTS Core Widgets SerialPort Network Svg REQUIRED)
set(QT_LIBRARIES Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Network Qt6::Svg)

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

set(SKY_CORE_SOURCES
    src/SkyRuntime.cpp
    src/SkyDeviceManager.cpp
    src/SkySessionRecorder.cpp
    src/SerialTelemetryLink.cpp
    src/TcpTelemetryLink.cpp
    src/SkyLocalIpcServer.cpp
    src/TcpWaveEncoding.cpp
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

set(NAV_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/GroundTelemetryService.cpp
    src/SkyDeviceConfigDialog.cpp
    src/TcpWavePanel.cpp
    src/RawDataParserWindow.cpp
    src/SessionViewerWindow.cpp
    src/TrajectoryViewerDialog.cpp
    src/RtkConfigDialog.cpp
    src/RtkStreamService.cpp
    src/CustomTitleBar.cpp
    src/WindowSizing.cpp
)

set(NAV_HEADERS
    include/AppTheme.h
    include/MainWindow.h
    include/GroundTelemetryService.h
    include/SkyDeviceConfigDialog.h
    include/TcpWavePanel.h
    include/RawDataParserWindow.h
    include/SessionViewerWindow.h
    include/TrajectoryViewerDialog.h
    include/RtkConfigDialog.h
    include/RtkStreamService.h
    include/CustomTitleBar.h
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
    resources/lucide/circle-x.svg
    resources/lucide/copy.svg
    resources/lucide/funnel.svg
    resources/lucide/key.svg
    resources/lucide/languages.svg
    resources/lucide/list-filter.svg
    resources/lucide/maximize.svg
    resources/lucide/menu.svg
    resources/lucide/minus.svg
    resources/lucide/moon.svg
    resources/lucide/panel-right-close.svg
    resources/lucide/panel-right-open.svg
    resources/lucide/pause.svg
    resources/lucide/play.svg
    resources/lucide/plug.svg
    resources/lucide/refresh-cw.svg
    resources/lucide/satellite.svg
    resources/lucide/sliders-vertical.svg
    resources/lucide/square-check-big.svg
    resources/lucide/square.svg
    resources/lucide/sun.svg
    resources/lucide/timer.svg
    resources/lucide/trash-2.svg
    resources/lucide/unplug.svg
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

if(MSVC)
    target_compile_options(VaporView PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /Zc:__cplusplus /permissive-)
endif()

target_include_directories(VaporView PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(VaporView PRIVATE
    ${QT_LIBRARIES}
    vaporview_app_theme
    vaporview_sky_core
    rtklib_strsvr
)

if(WIN32)
    target_link_libraries(VaporView PRIVATE dwmapi)
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
    add_executable(tcp_wave_encoding_test
        tests/tcp_wave_encoding_test.cpp
    )
    target_include_directories(tcp_wave_encoding_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(tcp_wave_encoding_test PRIVATE vaporview_sky_core)
    add_test(NAME tcp_wave_encoding_test COMMAND tcp_wave_encoding_test)

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

