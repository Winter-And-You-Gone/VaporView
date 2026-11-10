# Apply policies only to the products selected for this build.
# Embed symbols in project objects so static libraries retain complete Release
# debug information without a shared compiler PDB. Keep optimized code generation.
if(MSVC)
    function(vaporview_collect_symbol_targets directory output)
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(child IN LISTS children)
            vaporview_collect_symbol_targets("${child}" child_targets)
            list(APPEND targets ${child_targets})
        endforeach()
        set(${output} ${targets} PARENT_SCOPE)
    endfunction()
    vaporview_collect_symbol_targets("${CMAKE_SOURCE_DIR}" vaporview_symbol_targets)
    foreach(vaporview_target IN LISTS vaporview_symbol_targets)
        get_target_property(vaporview_target_type ${vaporview_target} TYPE)
        if(vaporview_target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            target_compile_options(${vaporview_target} PRIVATE "$<$<CONFIG:Release>:/Z7>")
            if(NOT vaporview_target_type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY)$")
                target_link_options(${vaporview_target} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>"
                    "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
            endif()
        endif()
    endforeach()
endif()
set(vaporview_warning_targets
    vaporview_protocol vaporview_telemetry_transport vaporview_app_theme
    vaporview_device_collectors vaporview_fpga_device_core vaporview_sky_core
    vaporview_sky_tui VaporView vaporview_main_window VaporViewSky
    VaporViewSkyCore VaporViewSkyTui)
foreach(vaporview_target IN LISTS vaporview_warning_targets)
    if(NOT TARGET ${vaporview_target})
        continue()
    endif()
    if(MSVC)
        target_compile_options(${vaporview_target} PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    else()
        target_compile_options(${vaporview_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
if(MSVC)
    foreach(vaporview_target IN ITEMS VaporView vaporview_main_window
            vaporview_device_collectors vaporview_fpga_device_core vaporview_sky_core
            VaporViewSky VaporViewSkyCore VaporViewSkyTui)
        if(TARGET ${vaporview_target})
            target_compile_options(${vaporview_target} PRIVATE
                /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h)
        endif()
    endforeach()
endif()
