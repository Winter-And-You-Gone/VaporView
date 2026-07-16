add_library(vaporview_session_format STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionSensorCsv.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionSensorCsv.h
)

target_include_directories(vaporview_session_format PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_session_format PUBLIC
    Qt6::Core
)
