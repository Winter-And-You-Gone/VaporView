add_library(vaporview_ground_device_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteTelemetryDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RemoteTelemetryDecoder.h
)

target_include_directories(vaporview_ground_device_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_device_core PUBLIC
    vaporview_protocol
    vaporview_geo_core
    Qt6::Core
)
