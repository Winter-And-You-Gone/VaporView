add_library(vaporview_ground_gateway STATIC
    GatewayService.cpp
    GatewayService.h
    OfflineQueue.cpp
    OfflineQueue.h
)

target_include_directories(vaporview_ground_gateway PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_gateway PUBLIC
    vaporview_ground_device_core
    Qt6::Network
)
