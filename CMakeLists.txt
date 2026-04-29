cmake_minimum_required(VERSION 3.16)

project(VaporView VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(BUILD_PYTHON_BINDINGS "Build Python bindings with pybind11" OFF)

find_package(Qt6 COMPONENTS Core Qml Quick QuickControls2 SerialPort Network REQUIRED)
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()
set(QT_LIBRARIES Qt6::Core Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::SerialPort Qt6::Network)

if(WIN32)
    find_program(QT_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS
            "${Qt6_DIR}/../../../bin"
            ${CMAKE_PREFIX_PATH}/bin
            ENV PATH
    )
endif()

find_package(Threads REQUIRED)
include(CTest)

set(UM982_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/um982_driver)
set(HIPNUC_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/hipnuc_driver)
set(RTKLIB_DIR ${CMAKE_SOURCE_DIR}/third_party/rtklib)
set(RTKLIB_STR2STR_SOURCE ${RTKLIB_DIR}/app/consapp/str2str/str2str.c)

set(RTKLIB_SOURCES
    ${RTKLIB_DIR}/src/stream.c
    ${RTKLIB_DIR}/src/streamsvr.c
    ${RTKLIB_DIR}/src/rtkcmn.c
    ${RTKLIB_DIR}/src/solution.c
    ${RTKLIB_DIR}/src/sbas.c
    ${RTKLIB_DIR}/src/geoid.c
    ${RTKLIB_DIR}/src/rcvraw.c
    ${RTKLIB_DIR}/src/rtcm.c
    ${RTKLIB_DIR}/src/rtcm2.c
    ${RTKLIB_DIR}/src/rtcm3.c
    ${RTKLIB_DIR}/src/rtcm3e.c
    ${RTKLIB_DIR}/src/preceph.c
    ${RTKLIB_DIR}/src/rcv/novatel.c
    ${RTKLIB_DIR}/src/rcv/ublox.c
    ${RTKLIB_DIR}/src/rcv/ss2.c
    ${RTKLIB_DIR}/src/rcv/crescent.c
    ${RTKLIB_DIR}/src/rcv/skytraq.c
    ${RTKLIB_DIR}/src/rcv/javad.c
    ${RTKLIB_DIR}/src/rcv/nvs.c
    ${RTKLIB_DIR}/src/rcv/binex.c
    ${RTKLIB_DIR}/src/rcv/rt17.c
    ${RTKLIB_DIR}/src/rcv/septentrio.c
)

add_library(rtklib_strsvr STATIC ${RTKLIB_SOURCES})
target_include_directories(rtklib_strsvr PUBLIC ${RTKLIB_DIR}/src)
target_compile_definitions(rtklib_strsvr PRIVATE
    ENAGLO
    ENAGAL
    ENAQZS
    ENACMP
    ENAIRN
    NFREQ=5
    NEXOBS=3
    SVR_REUSEADDR
)

if(MSVC)
    target_compile_definitions(rtklib_strsvr PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rtklib_strsvr PRIVATE /W0 /Zc:__cplusplus /permissive-)
else()
    target_compile_options(rtklib_strsvr PRIVATE -w)
endif()

if(WIN32)
    target_link_libraries(rtklib_strsvr PUBLIC ws2_32 winmm)
endif()

if(EXISTS ${RTKLIB_STR2STR_SOURCE})
    add_executable(str2str ${RTKLIB_STR2STR_SOURCE})
    target_include_directories(str2str PRIVATE ${RTKLIB_DIR}/src)
    target_link_libraries(str2str PRIVATE rtklib_strsvr)

    if(MSVC)
        target_compile_definitions(str2str PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(str2str PRIVATE /W0 /utf-8 /Zc:__cplusplus /permissive-)
    else()
        target_compile_options(str2str PRIVATE -w)
    endif()
endif()

set(NAV_SOURCES
    src/main.cpp
    src/VaporViewBackends.cpp
    src/serial_port.cpp
    src/data_collector.cpp
    src/TcpWaveEncoding.cpp
    src/RtkStreamService.cpp
    ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
)

set(NAV_HEADERS
    include/VaporViewBackends.h
    include/serial_port.h
    include/data_types.h
    include/data_collector.h
    include/TcpWaveEncoding.h
    include/RtkStreamService.h
    ${UM982_DRIVER_DIR}/pvtsln_data.hpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.h
)

set(NAV_UI_FILES
)

set(NAV_WIN_RESOURCES)
if(WIN32)
    list(APPEND NAV_WIN_RESOURCES resources/app.rc)
endif()

if(WIN32)
    add_executable(VaporView WIN32
        ${NAV_SOURCES}
        ${NAV_HEADERS}
        ${NAV_UI_FILES}
        ${NAV_WIN_RESOURCES}
    )
else()
    add_executable(VaporView
        ${NAV_SOURCES}
        ${NAV_HEADERS}
        ${NAV_UI_FILES}
    )
endif()

file(GLOB_RECURSE VAPORVIEW_QML_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_SOURCE_DIR}
    qml/*.qml
)

file(GLOB_RECURSE VAPORVIEW_QML_RESOURCE_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_SOURCE_DIR}
    qml/assets/*
)

qt_add_qml_module(VaporView
    URI VaporView
    VERSION 1.0
    QML_FILES
        ${VAPORVIEW_QML_FILES}
    RESOURCES
        ${VAPORVIEW_QML_RESOURCE_FILES}
)

if(MSVC)
    target_compile_options(VaporView PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /Zc:__cplusplus /permissive-)
endif()

target_include_directories(VaporView PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(VaporView PRIVATE
    ${QT_LIBRARIES}
    Threads::Threads
    rtklib_strsvr
)

add_custom_command(TARGET VaporView POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VaporView>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/app.ico
        $<TARGET_FILE_DIR:VaporView>/resources/app.ico
    COMMENT "Copying resources to build directory"
)

if(BUILD_TESTING)
    add_executable(tcp_wave_encoding_test
        tests/tcp_wave_encoding_test.cpp
        src/TcpWaveEncoding.cpp
        include/TcpWaveEncoding.h
    )
    target_include_directories(tcp_wave_encoding_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(tcp_wave_encoding_test PRIVATE Qt6::Core)
    add_test(NAME tcp_wave_encoding_test COMMAND tcp_wave_encoding_test)
endif()

if(WIN32 AND QT_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET VaporView POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --no-translations
            --no-system-dxc-compiler
            --qmldir
            ${CMAKE_SOURCE_DIR}/qml
            $<TARGET_FILE:VaporView>
        COMMENT "Deploying Qt runtime for VaporView"
    )
endif()

if(MSVC)
    target_compile_options(VaporView PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
else()
    target_compile_options(VaporView PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(BUILD_PYTHON_BINDINGS)
    set(PYTHON_BINDINGS_ENTRY ${CMAKE_SOURCE_DIR}/python/bindings.cpp)
    if(NOT EXISTS ${PYTHON_BINDINGS_ENTRY})
        message(FATAL_ERROR
            "BUILD_PYTHON_BINDINGS=ON is not supported in this repository because "
            "${PYTHON_BINDINGS_ENTRY} is missing. The Python bindings target is explicitly disabled "
            "until a real bindings entrypoint is added.")
    endif()

    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(VaporView_bindings
        ${PYTHON_BINDINGS_ENTRY}
        src/serial_port.cpp
        src/data_collector.cpp
        ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
        ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
    )

    target_include_directories(VaporView_bindings PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${UM982_DRIVER_DIR}
        ${HIPNUC_DRIVER_DIR}
    )

    if(MSVC)
        target_compile_options(VaporView_bindings PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /Zc:__cplusplus /permissive-)
    endif()

    target_link_libraries(VaporView_bindings PRIVATE
        Threads::Threads
    )
endif()

install(TARGETS VaporView
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

install(FILES ${NAV_RESOURCES}
    DESTINATION resources
)

