add_library(vaporview_wave_encoding STATIC
    ${CMAKE_SOURCE_DIR}/src/TcpWaveEncoding.cpp
    ${CMAKE_SOURCE_DIR}/include/TcpWaveEncoding.h
)

target_include_directories(vaporview_wave_encoding PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(vaporview_wave_encoding PUBLIC
    Qt6::Core
)
