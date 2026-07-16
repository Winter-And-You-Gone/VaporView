function(vaporview_configure_platform_runtime)
if(WIN32 AND QT_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET VaporView POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporView>
            $<TARGET_FILE:VaporView>
        COMMENT "Deploying Qt runtime for VaporView"
    )
    add_custom_command(TARGET VaporViewSky POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporViewSky>
            $<TARGET_FILE:VaporViewSky>
        COMMENT "Deploying Qt runtime for VaporViewSky"
    )
    add_custom_command(TARGET VaporViewSkyCore POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporViewSkyCore>
            $<TARGET_FILE:VaporViewSkyCore>
        COMMENT "Deploying Qt runtime for VaporViewSkyCore"
    )
    add_custom_command(TARGET VaporViewSkyTui POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE}
            --compiler-runtime
            --force
            --no-translations
            --no-system-dxc-compiler
            --dir
            $<TARGET_FILE_DIR:VaporViewSkyTui>
            $<TARGET_FILE:VaporViewSkyTui>
        COMMENT "Deploying Qt runtime for VaporViewSkyTui"
    )
endif()

if(WIN32)
    add_custom_command(TARGET VaporView POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporView>
            -D TARGET_FILE=$<TARGET_FILE:VaporView>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporView release runtime"
    )
    add_custom_command(TARGET VaporViewSky POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporViewSky>
            -D TARGET_FILE=$<TARGET_FILE:VaporViewSky>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporViewSky release runtime"
    )
    add_custom_command(TARGET VaporViewSkyCore POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporViewSkyCore>
            -D TARGET_FILE=$<TARGET_FILE:VaporViewSkyCore>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporViewSkyCore release runtime"
    )
    add_custom_command(TARGET VaporViewSkyTui POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=$<TARGET_FILE_DIR:VaporViewSkyTui>
            -D TARGET_FILE=$<TARGET_FILE:VaporViewSkyTui>
            -D STAGE_DIR=${CMAKE_BINARY_DIR}/Release
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_release.cmake
        COMMENT "Staging VaporViewSkyTui release runtime"
    )
endif()
endfunction()
