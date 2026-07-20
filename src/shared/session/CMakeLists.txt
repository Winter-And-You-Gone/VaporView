add_library(vaporview_session_format STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/RecordingOrigin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RecordingOrigin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionDeviceConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionDeviceConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionManifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionManifest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionPackageInitializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionPackageInitializer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionPackageLayout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionPackageLayout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionSensorCsv.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionSensorCsv.h
    ${CMAKE_CURRENT_SOURCE_DIR}/UnifiedRawDat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UnifiedRawDat.h
)

target_include_directories(vaporview_session_format PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_session_format PUBLIC
    Qt6::Core
)
