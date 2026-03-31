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
if(WIN32)
    option(ENABLE_TDLAS_CAPTURE "Enable TDLAS Ethernet capture via Npcap SDK" ON)
else()
    option(ENABLE_TDLAS_CAPTURE "Enable TDLAS Ethernet capture via Npcap SDK" OFF)
endif()

find_package(Qt6 COMPONENTS Core Widgets SerialPort Network REQUIRED)
set(QT_LIBRARIES Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Network)

if(WIN32 AND MINGW)
    find_program(QT_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS
            ${CMAKE_PREFIX_PATH}/bin
            ENV PATH
    )

    get_filename_component(MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
endif()

find_package(Threads REQUIRED)

set(UM982_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/um982_driver)
set(HIPNUC_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/hipnuc_driver)
set(RTKLIB_DIR ${CMAKE_SOURCE_DIR}/third_party/rtklib)
set(RTKLIB_STR2STR_SOURCE ${RTKLIB_DIR}/app/consapp/str2str/str2str.c)

set(NPCAP_INCLUDE_DIR "")
set(NPCAP_WPCAP_LIB "")
set(NPCAP_PACKET_LIB "")
if(WIN32 AND ENABLE_TDLAS_CAPTURE)
    set(_npcap_roots)
    if(DEFINED NPCAP_SDK_DIR)
        list(APPEND _npcap_roots "${NPCAP_SDK_DIR}")
    endif()
    if(DEFINED ENV{NPCAP_SDK_DIR})
        list(APPEND _npcap_roots "$ENV{NPCAP_SDK_DIR}")
    endif()
    list(APPEND _npcap_roots
        "C:/Program Files/Npcap SDK"
        "C:/Program Files (x86)/Npcap SDK"
        "C:/Program Files/Npcap"
        "C:/Program Files (x86)/Npcap"
    )

    foreach(_npcap_root IN LISTS _npcap_roots)
        cmake_path(NORMAL_PATH _npcap_root OUTPUT_VARIABLE _npcap_root_normalized)

        set(_npcap_include_candidate "${_npcap_root_normalized}/Include")
        set(_npcap_wpcap_candidate "${_npcap_root_normalized}/Lib/x64/wpcap.lib")
        set(_npcap_packet_candidate "${_npcap_root_normalized}/Lib/x64/Packet.lib")

        if(NOT NPCAP_INCLUDE_DIR AND EXISTS "${_npcap_include_candidate}/pcap.h")
            set(NPCAP_INCLUDE_DIR "${_npcap_include_candidate}")
        endif()
        if(NOT NPCAP_WPCAP_LIB AND EXISTS "${_npcap_wpcap_candidate}")
            set(NPCAP_WPCAP_LIB "${_npcap_wpcap_candidate}")
        endif()
        if(NOT NPCAP_PACKET_LIB AND EXISTS "${_npcap_packet_candidate}")
            set(NPCAP_PACKET_LIB "${_npcap_packet_candidate}")
        endif()

        if(NPCAP_INCLUDE_DIR AND NPCAP_WPCAP_LIB AND NPCAP_PACKET_LIB)
            break()
        endif()
    endforeach()

    if(NOT NPCAP_INCLUDE_DIR OR NOT NPCAP_WPCAP_LIB OR NOT NPCAP_PACKET_LIB)
        message(FATAL_ERROR
            "ENABLE_TDLAS_CAPTURE=ON requires the Npcap SDK (Include + Lib/x64). "
            "Set NPCAP_SDK_DIR or install the SDK in a standard path.")
    endif()
endif()

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
    src/MainWindow.cpp
    src/serial_port.cpp
    src/data_collector.cpp
    src/EthernetCaptureCollector.cpp
    src/RtkConfigDialog.cpp
    src/RtkStreamService.cpp
    ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
)

set(NAV_HEADERS
    include/MainWindow.h
    include/serial_port.h
    include/data_types.h
    include/data_collector.h
    include/EthernetCaptureCollector.h
    include/RtkConfigDialog.h
    include/RtkStreamService.h
    ${UM982_DRIVER_DIR}/pvtsln_data.hpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.h
)

set(NAV_UI_FILES
)

set(NAV_RESOURCES
    resources/modern_style.qss
    resources/combo_arrow_down.xpm
    resources/combo_arrow_up.xpm
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
        ${NAV_RESOURCES}
        ${NAV_WIN_RESOURCES}
    )
else()
    add_executable(VaporView
        ${NAV_SOURCES}
        ${NAV_HEADERS}
        ${NAV_UI_FILES}
        ${NAV_RESOURCES}
    )
endif()

if(MSVC)
    target_compile_options(VaporView PRIVATE /FI"${CMAKE_SOURCE_DIR}/include/compiler_compat.h" /Zc:__cplusplus /permissive-)
endif()

target_include_directories(VaporView PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${RTKLIB_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
    ${NPCAP_INCLUDE_DIR}
)

target_link_libraries(VaporView PRIVATE
    ${QT_LIBRARIES}
    Threads::Threads
    rtklib_strsvr
)

if(WIN32 AND ENABLE_TDLAS_CAPTURE)
    target_compile_definitions(VaporView PRIVATE ENABLE_TDLAS_CAPTURE=1)
endif()

add_custom_command(TARGET VaporView POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
        ${CMAKE_BINARY_DIR}/resources/modern_style.qss
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
        ${CMAKE_BINARY_DIR}/resources/combo_arrow_down.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
        ${CMAKE_BINARY_DIR}/resources/combo_arrow_up.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/app.ico
        ${CMAKE_BINARY_DIR}/resources/app.ico
    COMMENT "Copying resources to build directory"
)

if(WIN32 AND MINGW)
    if(QT_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET VaporView POST_BUILD
            COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
                --release
                --compiler-runtime
                --no-translations
                $<TARGET_FILE:VaporView>
            COMMENT "Deploying Qt runtime for VaporView"
        )
    endif()

    foreach(_mingw_runtime_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        if(EXISTS "${MINGW_RUNTIME_DIR}/${_mingw_runtime_dll}")
            add_custom_command(TARGET VaporView POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_RUNTIME_DIR}/${_mingw_runtime_dll}"
                    "$<TARGET_FILE_DIR:VaporView>/${_mingw_runtime_dll}"
                COMMENT "Copying ${_mingw_runtime_dll} to build directory"
            )
        endif()
    endforeach()
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
        target_compile_options(VaporView_bindings PRIVATE /FI"${CMAKE_SOURCE_DIR}/include/compiler_compat.h" /Zc:__cplusplus /permissive-)
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

