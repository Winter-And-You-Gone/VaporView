set(GEO_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/geo/CoordinateTransform.cpp
    ${CMAKE_SOURCE_DIR}/src/geo/TrajectoryBuffer.cpp
    ${CMAKE_SOURCE_DIR}/src/geo/TrajectoryReplay.cpp
    ${CMAKE_SOURCE_DIR}/src/geo/TrajectoryQuality.cpp
    ${CMAKE_SOURCE_DIR}/src/geo/SessionTrackReader.cpp
    ${CMAKE_SOURCE_DIR}/src/geo/TrajectoryHeatmap.cpp
)

set(GEO_CORE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/geo/CoordinateTransform.h
    ${CMAKE_SOURCE_DIR}/include/geo/GeoTypes.h
    ${CMAKE_SOURCE_DIR}/include/geo/TrajectoryBuffer.h
    ${CMAKE_SOURCE_DIR}/include/geo/TrajectoryReplay.h
    ${CMAKE_SOURCE_DIR}/include/geo/TrajectoryQuality.h
    ${CMAKE_SOURCE_DIR}/include/geo/SessionTrackReader.h
    ${CMAKE_SOURCE_DIR}/include/geo/TrajectoryHeatmap.h
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
    vaporview_session_format
)
