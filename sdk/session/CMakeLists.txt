include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(sdk_version 1.0.0)
set(sdk_component VaporViewSessionSdk)
set(sdk_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/VaporViewSessionSdk)

add_library(vaporview_session_sdk SHARED SessionSdk.cpp)
add_library(VaporView::SessionSdk ALIAS vaporview_session_sdk)
set_target_properties(vaporview_session_sdk PROPERTIES
    EXPORT_NAME SessionSdk
    VERSION ${sdk_version}
    SOVERSION 1
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# The Linux installer stages libraries beside the ground executable.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(vaporview_session_sdk PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_property(TARGET VaporView APPEND PROPERTY BUILD_RPATH "$ORIGIN")
endif()
target_compile_features(vaporview_session_sdk PUBLIC cxx_std_17)
target_compile_definitions(vaporview_session_sdk PRIVATE
    VAPORVIEW_SESSION_SDK_BUILD VAPORVIEW_SESSION_SDK_VERSION="${sdk_version}")
target_include_directories(vaporview_session_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(vaporview_session_sdk PUBLIC Qt6::Core PRIVATE vaporview_water_calibration_core)
target_link_libraries(vaporview_ground_session_ui PRIVATE vaporview_session_sdk)

configure_package_config_file(VaporViewSessionSdkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/VaporViewSessionSdkConfig.cmake"
    INSTALL_DESTINATION "${sdk_cmake_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VaporViewSessionSdkConfigVersion.cmake"
    VERSION "${sdk_version}" COMPATIBILITY SameMajorVersion)
install(TARGETS vaporview_session_sdk EXPORT VaporViewSessionSdkTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT ${sdk_component}
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT ${sdk_component}
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT ${sdk_component})
install(EXPORT VaporViewSessionSdkTargets NAMESPACE VaporView::
    DESTINATION "${sdk_cmake_dir}" COMPONENT ${sdk_component})
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VaporViewSessionSdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VaporViewSessionSdkConfigVersion.cmake"
    DESTINATION "${sdk_cmake_dir}" COMPONENT ${sdk_component})
install(FILES include/vaporview/SessionSdk.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vaporview" COMPONENT ${sdk_component})
install(FILES "${CMAKE_SOURCE_DIR}/docs/session_sdk.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/VaporViewSessionSdk" COMPONENT ${sdk_component})
install(DIRECTORY example/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/VaporViewSessionSdk/example" COMPONENT ${sdk_component})

add_executable(vaporview_session_example example/main.cpp)
target_link_libraries(vaporview_session_example PRIVATE VaporView::SessionSdk)

if(BUILD_TESTING)
    add_executable(session_sdk_test "${CMAKE_SOURCE_DIR}/tests/session_sdk_test.cpp")
    target_link_libraries(session_sdk_test PRIVATE VaporView::SessionSdk)
endif()
