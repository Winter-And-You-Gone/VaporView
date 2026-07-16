add_library(vaporview_ground_device_policy STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/DeviceRatePolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DeviceRatePolicy.h
)

target_include_directories(vaporview_ground_device_policy PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_device_policy PUBLIC
    Qt6::Core
)

add_library(vaporview_ground_temperature_command_state STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/TemperatureCommandState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TemperatureCommandState.h
)

target_include_directories(vaporview_ground_temperature_command_state PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_temperature_command_state PUBLIC
    Qt6::Core
)

add_library(vaporview_ground_device_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundTelemetryService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundTelemetryService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ImuConfigurationService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ImuConfigurationService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CollectorRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CollectorRegistry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/EpsilonConfigurationService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EpsilonConfigurationService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/LocalDeviceConnectionController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LocalDeviceConnectionController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteTelemetryDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteTelemetryDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteTelemetryState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteTelemetryState.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteSkyController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteSkyController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SerialPortDetectionService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SerialPortDetectionService.h
)

target_include_directories(vaporview_ground_device_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_device_core PUBLIC
    vaporview_ground_device_policy
    vaporview_protocol
    vaporview_geo_core
    Qt6::Core
)

target_link_libraries(vaporview_ground_device_core PRIVATE
    vaporview_sky_core
)
