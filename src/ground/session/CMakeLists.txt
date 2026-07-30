if(NOT TARGET vaporview_ground_session_core)
add_library(vaporview_ground_session_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundRecordingService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GroundRecordingService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RecordingSessionLayout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RecordingSessionLayout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RecordingScheduleController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RecordingScheduleController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionCsv.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionCsv.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionExportService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionExportService.h
    ${CMAKE_SOURCE_DIR}/include/ground/SessionData.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionIndex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionIndex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionLoader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionPlaybackController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionPlaybackController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTimeFormat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTimeFormat.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTimelineModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTimelineModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTrajectoryRenderLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTrajectoryRenderLoader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTrajectoryController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionTrajectoryController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionWaveformRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionWaveformRepository.h
)

target_include_directories(vaporview_ground_session_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_session_core PUBLIC
    vaporview_geo_core
    vaporview_session_format
    vaporview_wave_encoding
    Qt6::Core
)

endif()

if(VAPORVIEW_SESSION_CORE_ONLY)
    return()
endif()

if(NOT TARGET vaporview_ground_session_ui)
add_library(vaporview_ground_session_ui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionMapCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionMapCoordinator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionViewerPages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionViewerPages.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionViewerWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionViewerWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionViewerWidgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionViewerWidgets.h
)

target_include_directories(vaporview_ground_session_ui PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vaporview_ground_session_ui PUBLIC
    Qt6::Widgets
)

target_link_libraries(vaporview_ground_session_ui PRIVATE
    vaporview_app_config
    vaporview_app_theme
    vaporview_ground_session_core
    vaporview_ground_trajectory
    vaporview_ground_ui_support
    vaporview_ground_wave
    Qt6::Concurrent
)

endif()
