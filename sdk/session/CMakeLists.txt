include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(sdk_version 1.1.0)
set(sdk_component VaporViewSessionSdk)
set(sdk_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/VaporViewSessionSdk)

add_library(vaporview_session_sdk SHARED SessionSdk.cpp SessionSdkC.cpp)
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
target_compile_features(vaporview_session_sdk PRIVATE cxx_std_17)
target_compile_definitions(vaporview_session_sdk PRIVATE
    VAPORVIEW_SESSION_SDK_BUILD VAPORVIEW_SESSION_SDK_VERSION="${sdk_version}")
target_include_directories(vaporview_session_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(vaporview_session_sdk PRIVATE Qt6::Core vaporview_water_calibration_core)
target_link_libraries(vaporview_session_sdk INTERFACE $<BUILD_INTERFACE:Qt6::Core>)
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
# Both imported targets address the same binary. Only the legacy target
# receives the Qt usage requirements in the installed package config.
install(EXPORT VaporViewSessionSdkTargets NAMESPACE VaporViewBinary::
    FILE VaporViewSessionSdkBinaryTargets.cmake
    DESTINATION "${sdk_cmake_dir}" COMPONENT ${sdk_component})
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VaporViewSessionSdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VaporViewSessionSdkConfigVersion.cmake"
    DESTINATION "${sdk_cmake_dir}" COMPONENT ${sdk_component})
install(FILES include/vaporview/SessionSdk.h include/vaporview/SessionSdkC.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vaporview" COMPONENT ${sdk_component})
install(FILES "${CMAKE_SOURCE_DIR}/docs/session_sdk.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/VaporViewSessionSdk" COMPONENT ${sdk_component})
install(DIRECTORY example example_c example_python
    DESTINATION "${CMAKE_INSTALL_DATADIR}/VaporViewSessionSdk" COMPONENT ${sdk_component}
    PATTERN "__pycache__" EXCLUDE)
install(FILES python/vaporview_session.py DESTINATION python COMPONENT ${sdk_component})

set(sdk_licenses "${CMAKE_INSTALL_DATADIR}/VaporViewSessionSdk/licenses")
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "${sdk_licenses}"
    RENAME VaporView-GPL-3.0.txt COMPONENT ${sdk_component})
if(WIN32)
    install(FILES "$<TARGET_FILE:Qt6::Core>" DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT ${sdk_component})
    configure_file(QtRuntimeNotice.md.in "${CMAKE_CURRENT_BINARY_DIR}/QtRuntimeNotice.md" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QtRuntimeNotice.md" licenses/LGPL-3.0.txt
        DESTINATION "${sdk_licenses}" COMPONENT ${sdk_component})
    # Standard layouts of a source-built Qt and the Qt online installer.
    find_path(VAPORVIEW_QTCORE_DOC_DIR NAMES qtcore-index.html
        HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_DOCS}/qtcore"
              "${QT6_INSTALL_PREFIX}/../../Docs/Qt-${Qt6_VERSION}/qtcore"
        DOC "Directory containing the matching Qt Core HTML attribution notices")
    install(DIRECTORY "${VAPORVIEW_QTCORE_DOC_DIR}/" DESTINATION "${sdk_licenses}/qtcore"
        COMPONENT ${sdk_component} FILES_MATCHING PATTERN "qtcore-attribution-*.html"
        PATTERN "images" EXCLUDE PATTERN "style" EXCLUDE PATTERN "scripts" EXCLUDE)
endif()

add_executable(vaporview_session_example example/main.cpp)
target_link_libraries(vaporview_session_example PRIVATE VaporView::SessionSdk)

if(BUILD_TESTING)
    add_executable(session_sdk_test "${CMAKE_SOURCE_DIR}/tests/session_sdk_test.cpp")
    target_link_libraries(session_sdk_test PRIVATE VaporView::SessionSdk)
endif()
