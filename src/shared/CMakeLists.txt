add_library(vaporview_logging STATIC
    ${CMAKE_SOURCE_DIR}/src/shared/logging/BoundedLogRecord.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/logging/BoundedLogRecord.h
    ${CMAKE_SOURCE_DIR}/src/shared/logging/LogService.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/logging/LogQueuePolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/logging/LogQueuePolicy.h
    ${CMAKE_SOURCE_DIR}/include/LogRecord.h
    ${CMAKE_SOURCE_DIR}/include/LogService.h
)

target_include_directories(vaporview_logging PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/shared
)

target_link_libraries(vaporview_logging PUBLIC
    Qt6::Core
)

if(BUILD_TESTING)
    target_compile_definitions(vaporview_logging PUBLIC VAPORVIEW_LOGGING_TEST_HOOKS)
endif()

set(PROTOCOL_SOURCES
    ${CMAKE_SOURCE_DIR}/src/shared/telemetry/TelemetryCodec.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/telemetry/TelemetryLink.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/config/SkyConfig.cpp
)

set(PROTOCOL_HEADERS
    ${CMAKE_SOURCE_DIR}/include/TelemetryTypes.h
    ${CMAKE_SOURCE_DIR}/include/TelemetryCodec.h
    ${CMAKE_SOURCE_DIR}/include/TelemetryLink.h
    ${CMAKE_SOURCE_DIR}/include/SkyConfig.h
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
    vaporview_logging
)

add_library(vaporview_app_theme STATIC
    ${CMAKE_SOURCE_DIR}/src/shared/theme/AppTheme.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/theme/SingleLevelPopupComboBox.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/theme/SingleLevelPopupMenu.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/theme/TopLevelCardStyle.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/theme/AppTheme.h
    ${CMAKE_SOURCE_DIR}/src/shared/theme/SingleLevelPopupComboBox.h
    ${CMAKE_SOURCE_DIR}/src/shared/theme/SingleLevelPopupMenu.h
    ${CMAKE_SOURCE_DIR}/src/shared/theme/TopLevelCardStyle.h
)

add_library(vaporview_app_config STATIC
    ${CMAKE_SOURCE_DIR}/src/shared/config/ApplicationConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/config/ApplicationConfig.h
    ${CMAKE_SOURCE_DIR}/src/shared/config/SettingsWriteBarrier.h
)

target_include_directories(vaporview_app_config PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_app_config PUBLIC
    Qt6::Core
)

target_include_directories(vaporview_app_theme PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_app_theme PUBLIC
    Qt6::Widgets
)

add_library(vaporview_wave_encoding STATIC
    ${CMAKE_SOURCE_DIR}/src/shared/wave/TcpWaveEncoding.cpp
    ${CMAKE_SOURCE_DIR}/include/TcpWaveEncoding.h
)

target_include_directories(vaporview_wave_encoding PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(vaporview_wave_encoding PUBLIC
    Qt6::Core
)

add_subdirectory(session)
