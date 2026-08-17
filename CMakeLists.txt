cmake_minimum_required(VERSION 3.16)

project(VaporView VERSION 1.0.22 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(vaporview_set_windows_as_invoker_manifest target_name)
    if(WIN32)
        target_link_options(${target_name} PRIVATE
            "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
        )
    endif()
endfunction()

if(WIN32)
    add_executable(VaporViewUpdateRelauncher WIN32
        src/app/update_relauncher_main.cpp
    )
    target_compile_definitions(VaporViewUpdateRelauncher PRIVATE UNICODE _UNICODE)
    target_link_libraries(VaporViewUpdateRelauncher PRIVATE advapi32 shell32)
    vaporview_set_windows_as_invoker_manifest(VaporViewUpdateRelauncher)

    add_executable(VaporViewPermissionTool
        src/app/permission_tool_main.cpp
    )
    target_compile_definitions(VaporViewPermissionTool PRIVATE UNICODE _UNICODE)
    target_link_libraries(VaporViewPermissionTool PRIVATE advapi32 shell32 rpcrt4)
    vaporview_set_windows_as_invoker_manifest(VaporViewPermissionTool)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}")
endforeach()

option(BUILD_PYTHON_BINDINGS "Build Python bindings with pybind11" OFF)
option(VAPORVIEW_ENABLE_OSGEARTH "Enable osgEarth 3D local map viewer" OFF)
set(VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR
    "${CMAKE_SOURCE_DIR}/.local_deps/vcpkg_installed"
    CACHE PATH
    "Project-local vcpkg installed directory used for optional native dependencies"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(Dependencies)
include(PlatformConfig)
include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(CompilerOptions)
include(PythonBindings)
include(InstallRules)
