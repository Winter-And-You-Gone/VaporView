if(MSVC)
    target_compile_options(VaporView PRIVATE
        /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h
        /Zc:__cplusplus
        /permissive-
    )
    target_compile_options(vaporview_main_window PRIVATE
        /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h
        /Zc:__cplusplus
        /permissive-
    )
endif()

if(MSVC)
    target_compile_options(vaporview_protocol PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_app_theme PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_sky_core PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_sky_tui PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporView PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(vaporview_main_window PRIVATE /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporViewSky PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporViewSkyCore PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
    target_compile_options(VaporViewSkyTui PRIVATE /FI${CMAKE_SOURCE_DIR}/include/compiler_compat.h /W4 /utf-8 /Zc:__cplusplus /permissive-)
else()
    target_compile_options(vaporview_protocol PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(vaporview_app_theme PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(vaporview_sky_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(vaporview_sky_tui PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporView PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporViewSky PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporViewSkyCore PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_options(VaporViewSkyTui PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
