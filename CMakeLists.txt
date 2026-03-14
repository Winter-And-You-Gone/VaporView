cmake_minimum_required(VERSION 3.16)
project(VaproView VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(USE_QT6 "Use Qt6 instead of Qt5" OFF)
option(BUILD_PYTHON_BINDINGS "Build Python bindings with pybind11" OFF)

if(USE_QT6)
    find_package(Qt6 COMPONENTS Core Widgets SerialPort REQUIRED)
    set(QT_LIBRARIES Qt6::Core Qt6::Widgets Qt6::SerialPort)
else()
    find_package(Qt5 COMPONENTS Core Widgets SerialPort REQUIRED)
    set(QT_LIBRARIES Qt5::Core Qt5::Widgets Qt5::SerialPort)
endif()

find_package(Threads REQUIRED)

set(UM982_DRIVER_DIR ${CMAKE_SOURCE_DIR}/../um982_driver)
set(HIPNUC_DRIVER_DIR ${CMAKE_SOURCE_DIR}/../products-master/drivers)

set(NAV_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/serial_port.cpp
    src/data_collector.cpp
    src/RtkConfigDialog.cpp
    ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
)

set(NAV_HEADERS
    include/MainWindow.h
    include/serial_port.h
    include/data_types.h
    include/data_collector.h
    include/RtkConfigDialog.h
    ${UM982_DRIVER_DIR}/pvtsln_data.hpp
    ${HIPNUC_DRIVER_DIR}/hipnuc_dec.h
)

set(NAV_UI_FILES
)

set(NAV_RESOURCES
    resources/modern_style.qss
    resources/combo_arrow_down.xpm
)

set(NAV_WIN_RESOURCES)
if(WIN32)
    list(APPEND NAV_WIN_RESOURCES resources/app.rc)
endif()

if(WIN32)
    add_executable(VaproView WIN32
        ${NAV_SOURCES}
        ${NAV_HEADERS}
        ${NAV_UI_FILES}
        ${NAV_RESOURCES}
        ${NAV_WIN_RESOURCES}
    )
else()
    add_executable(VaproView
        ${NAV_SOURCES}
        ${NAV_HEADERS}
        ${NAV_UI_FILES}
        ${NAV_RESOURCES}
    )
endif()

if(MSVC)
    target_compile_options(VaproView PRIVATE /FI"${CMAKE_SOURCE_DIR}/include/compiler_compat.h")
endif()

target_include_directories(VaproView PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${UM982_DRIVER_DIR}
    ${HIPNUC_DRIVER_DIR}
)

target_link_libraries(VaproView PRIVATE
    ${QT_LIBRARIES}
    Threads::Threads
)

add_custom_command(TARGET VaproView POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
        ${CMAKE_BINARY_DIR}/resources/modern_style.qss
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
        ${CMAKE_BINARY_DIR}/resources/combo_arrow_down.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/app.ico
        ${CMAKE_BINARY_DIR}/resources/app.ico
    COMMENT "Copying resources to build directory"
)

if(MSVC)
    target_compile_options(VaproView PRIVATE /W4 /utf-8)
else()
    target_compile_options(VaproView PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 CONFIG)

    if(pybind11_FOUND)
        pybind11_add_module(VaproView_bindings
            python/bindings.cpp
            src/serial_port.cpp
            src/data_collector.cpp
            ${UM982_DRIVER_DIR}/pvtsln_parser.cpp
            ${HIPNUC_DRIVER_DIR}/hipnuc_dec.c
        )

        target_include_directories(VaproView_bindings PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${UM982_DRIVER_DIR}
            ${HIPNUC_DRIVER_DIR}
        )

        if(MSVC)
            target_compile_options(VaproView_bindings PRIVATE /FI"${CMAKE_SOURCE_DIR}/include/compiler_compat.h")
        endif()

        target_link_libraries(VaproView_bindings PRIVATE
            Threads::Threads
        )
    else()
        message(WARNING "pybind11 not found, Python bindings will not be built")
    endif()
endif()

install(TARGETS VaproView
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

install(FILES ${NAV_RESOURCES}
    DESTINATION resources
)
