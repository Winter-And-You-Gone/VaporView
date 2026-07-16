find_package(Qt6 COMPONENTS Core Widgets SerialPort Network Svg Concurrent REQUIRED)
set(QT_LIBRARIES Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::Network Qt6::Svg Qt6::Concurrent)

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

set(UM982_DRIVER_DIR "${CMAKE_SOURCE_DIR}/third_party/um982_driver")
set(HIPNUC_DRIVER_DIR "${CMAKE_SOURCE_DIR}/third_party/hipnuc_driver")
set(RTKLIB_DIR "${CMAKE_SOURCE_DIR}/third_party/rtklib")

function(vaporview_stage_osgearth_runtime target_name)
    if(TARGET VaporViewOsgEarthRuntime)
        add_dependencies(${target_name} VaporViewOsgEarthRuntime)
    else()
        message(WARNING
            "No project-local osgEarth runtime target found for ${target_name}; "
            "runtime DLLs must be available on PATH."
        )
    endif()
endfunction()
