add_library(vaporview_device_collectors STATIC
    ${CMAKE_SOURCE_DIR}/src/sky/devices/Ai8TemperatureControllerCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/Ai8TemperatureControllerProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/TemperatureControllerProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/serial_port.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/data_collector.cpp
    ${CMAKE_SOURCE_DIR}/include/Ai8TemperatureControllerCollector.h
    ${CMAKE_SOURCE_DIR}/include/Ai8TemperatureControllerProtocol.h
    ${CMAKE_SOURCE_DIR}/include/TemperatureControllerProtocol.h
    ${CMAKE_SOURCE_DIR}/include/serial_port.h
    ${CMAKE_SOURCE_DIR}/include/data_collector.h
    ${CMAKE_SOURCE_DIR}/include/EpsilonProtocol.h
    ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
    ${UM982_DRIVER_DIR}/pvtsln_data.hpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.h
)
target_include_directories(vaporview_device_collectors PUBLIC
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src
    ${UM982_DRIVER_DIR} ${HIPNUC_DRIVER_DIR})
target_link_libraries(vaporview_device_collectors PUBLIC
    vaporview_protocol vaporview_geo_core vaporview_ppk
    vaporview_session_format vaporview_wave_encoding
    Qt6::Core Qt6::SerialPort Qt6::Network Threads::Threads)

# Both deployment roles share the USB controller and sensor decoder; neither
# needs to link the other's application or open the same USB endpoint twice.
add_library(vaporview_fpga_device_core STATIC
    ${CMAKE_SOURCE_DIR}/src/ground/devices/FpgaDeviceSession.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/devices/FpgaDeviceController.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/devices/FpgaSensorDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/devices/FpgaSensorAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/devices/FpgaUsbTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/devices/FpgaCyApiTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/fpga/FpgaTelemetry.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/fpga/FpgaSerialBudget.cpp
    ${CMAKE_SOURCE_DIR}/include/FpgaDeviceSession.h
    ${CMAKE_SOURCE_DIR}/include/FpgaDeviceController.h
    ${CMAKE_SOURCE_DIR}/include/FpgaControlConfig.h
    ${CMAKE_SOURCE_DIR}/include/FpgaSensorDecoder.h
    ${CMAKE_SOURCE_DIR}/include/FpgaSensorAdapter.h
    ${CMAKE_SOURCE_DIR}/include/FpgaUsbTransport.h
    ${CMAKE_SOURCE_DIR}/include/FpgaCyApiTransport.h
    ${CMAKE_SOURCE_DIR}/include/FpgaTelemetry.h
    ${CMAKE_SOURCE_DIR}/include/FpgaSerialBudget.h
)
target_include_directories(vaporview_fpga_device_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vaporview_fpga_device_core PUBLIC
    vaporview_protocol vaporview_session_format vaporview_device_collectors Qt6::Core)
if(WIN32)
    set(VAPORVIEW_CYAPI_ROOT "" CACHE PATH "Cypress USB Suite SDK root (optional CyAPI backend)")
    find_path(VAPORVIEW_CYAPI_INCLUDE_DIR CyAPI.h
        HINTS "${VAPORVIEW_CYAPI_ROOT}"
        PATH_SUFFIXES inc include library/cpp/inc)
    find_library(VAPORVIEW_CYAPI_LIBRARY NAMES CyAPI
        HINTS "${VAPORVIEW_CYAPI_ROOT}"
        PATH_SUFFIXES lib/x64 lib/x64/Release library/cpp/lib/x64 library/cpp/lib/x64/Release)
    if(VAPORVIEW_CYAPI_INCLUDE_DIR AND VAPORVIEW_CYAPI_LIBRARY)
        target_include_directories(vaporview_fpga_device_core PRIVATE "${VAPORVIEW_CYAPI_INCLUDE_DIR}")
        target_link_libraries(vaporview_fpga_device_core PRIVATE "${VAPORVIEW_CYAPI_LIBRARY}")
        target_compile_definitions(vaporview_fpga_device_core PUBLIC VAPORVIEW_HAS_CYAPI=1)
    endif()
endif()

set(SKY_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sky/core/SkyRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/SkyDeviceManager.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/recording/SkySessionRecorder.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/SerialTelemetryLink.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/TcpTelemetryLink.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/ipc/SkyLocalIpcServer.cpp
    ${CMAKE_SOURCE_DIR}/src/sky/devices/SkyFpgaBackend.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/devices/SerialPortDetectionService.cpp
)

set(SKY_CORE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/SkyRuntime.h
    ${CMAKE_SOURCE_DIR}/include/SkyDashboardTypes.h
    ${CMAKE_SOURCE_DIR}/include/SkyDeviceManager.h
    ${CMAKE_SOURCE_DIR}/include/SkyFpgaBackend.h
    ${CMAKE_SOURCE_DIR}/include/SkySessionRecorder.h
    ${CMAKE_SOURCE_DIR}/include/SerialTelemetryLink.h
    ${CMAKE_SOURCE_DIR}/include/TcpTelemetryLink.h
    ${CMAKE_SOURCE_DIR}/include/SkyLocalIpcServer.h
    ${CMAKE_SOURCE_DIR}/include/TcpWaveEncoding.h
    ${CMAKE_SOURCE_DIR}/include/data_types.h
    ${CMAKE_SOURCE_DIR}/src/ground/devices/SerialPortDetectionService.h
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
    vaporview_device_collectors
    vaporview_fpga_device_core
    vaporview_geo_core
    vaporview_ppk
    vaporview_protocol
    vaporview_session_format
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

if(BUILD_TESTING)
    target_compile_definitions(vaporview_sky_tui PUBLIC
        VAPORVIEW_SKY_TUI_TESTING=1
    )
endif()

target_link_libraries(vaporview_sky_tui PUBLIC
    vaporview_app_theme
    vaporview_protocol
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
)
