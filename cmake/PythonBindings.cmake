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
        vaporview_sky_core
        Threads::Threads
    )
endif()
