add_library(vaporview_ppk_observation STATIC EpsilonRawSatellite.cpp ObservationStore.cpp ObservationStore.h)
target_include_directories(vaporview_ppk_observation PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vaporview_ppk_observation PUBLIC Qt6::Core vaporview_logging)

add_library(vaporview_ppk STATIC EpsilonRinexWriter.cpp EpsilonRinexWriter.h
    PpkNavigation.cpp PpkNavigation.h AttitudeStore.cpp AttitudeStore.h PpkProcessor.cpp PpkProcessor.h
    SessionNavigationSource.cpp SessionNavigationSource.h)
target_include_directories(vaporview_ppk PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vaporview_ppk PUBLIC vaporview_ppk_observation rtklib_strsvr)
target_compile_definitions(vaporview_ppk PUBLIC NOMINMAX)
