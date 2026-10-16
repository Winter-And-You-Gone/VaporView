# Apply policies only to the products selected for this build.
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
