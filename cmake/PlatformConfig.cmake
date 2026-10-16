function(vaporview_configure_platform_runtime)
    if(NOT WIN32)
        return()
    endif()
    foreach(vaporview_product IN ITEMS VaporView VaporViewSky VaporViewSkyCore VaporViewSkyTui)
        if(NOT TARGET ${vaporview_product})
            continue()
        endif()
        if(QT_WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET ${vaporview_product} POST_BUILD
                COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
                    --compiler-runtime --force --no-translations --no-system-dxc-compiler
                    --dir $<TARGET_FILE_DIR:${vaporview_product}>
                    $<TARGET_FILE:${vaporview_product}>
                COMMENT "Deploying Qt runtime for ${vaporview_product}"
            )
        endif()
        add_custom_command(TARGET ${vaporview_product} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -D SOURCE_DIR=$<TARGET_FILE_DIR:${vaporview_product}>
                -D TARGET_FILE=$<TARGET_FILE:${vaporview_product}>
                -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
                -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
            COMMENT "Staging ${vaporview_product} release runtime"
        )
    endforeach()
endfunction()
