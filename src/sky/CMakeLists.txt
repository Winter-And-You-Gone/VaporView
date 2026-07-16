set(SKY_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sky/core/SkyRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/SkyDeviceManager.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/recording/SkySessionRecorder.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/SerialTelemetryLink.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/TcpTelemetryLink.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/ipc/SkyLocalIpcServer.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/TemperatureControllerProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/serial_port.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/data_collector.cpp
    ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
)

set(SKY_CORE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/SkyRuntime.h
    ${CMAKE_SOURCE_DIR}/include/SkyDashboardTypes.h
    ${CMAKE_SOURCE_DIR}/include/SkyDeviceManager.h
    ${CMAKE_SOURCE_DIR}/include/SkySessionRecorder.h
    ${CMAKE_SOURCE_DIR}/include/SerialTelemetryLink.h
    ${CMAKE_SOURCE_DIR}/include/TcpTelemetryLink.h
    ${CMAKE_SOURCE_DIR}/include/SkyLocalIpcServer.h
    ${CMAKE_SOURCE_DIR}/include/TcpWaveEncoding.h
    ${CMAKE_SOURCE_DIR}/include/TemperatureControllerProtocol.h
    ${CMAKE_SOURCE_DIR}/include/serial_port.h
    ${CMAKE_SOURCE_DIR}/include/data_types.h
    ${CMAKE_SOURCE_DIR}/include/data_collector.h
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
    vaporview_geo_core
    vaporview_protocol
    vaporview_wave_encoding
    Qt6::Core
    Qt6::SerialPort
    Qt6::Network
    Threads::Threads
)

set(SKY_TUI_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sky/tui/SkyTuiApp.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/tui/SkyTuiController.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/tui/SkyTuiTheme.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/tui/SkyStartupScreen.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/ipc/SkyLocalIpcClient.cpp
)

set(SKY_TUI_COMMON_HEADERS
    ${CMAKE_SOURCE_DIR}/include/SkyTuiApp.h
    ${CMAKE_SOURCE_DIR}/include/SkyTuiController.h
    ${CMAKE_SOURCE_DIR}/include/SkyTuiModel.h
    ${CMAKE_SOURCE_DIR}/include/SkyTuiTheme.h
    ${CMAKE_SOURCE_DIR}/include/SkyTuiOptions.h
    ${CMAKE_SOURCE_DIR}/include/SkyStartupScreen.h
    ${CMAKE_SOURCE_DIR}/include/SkyLocalIpcClient.h
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
