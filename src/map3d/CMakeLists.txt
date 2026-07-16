if(VAPORVIEW_ENABLE_OSGEARTH)
    find_package(Qt6 COMPONENTS OpenGLWidgets REQUIRED)
    if(EXISTS "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows")
        list(PREPEND CMAKE_PREFIX_PATH "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows")
    endif()
    find_package(osgEarth CONFIG REQUIRED)
    find_package(OpenSceneGraph REQUIRED COMPONENTS osg osgDB osgGA osgViewer osgUtil)
    find_package(GDAL CONFIG REQUIRED)

    if(TARGET osgEarth::osgEarth)
        set(OSGEARTH_TARGET osgEarth::osgEarth)
    elseif(TARGET osgEarth)
        set(OSGEARTH_TARGET osgEarth)
    else()
        message(FATAL_ERROR "osgEarth package was found but no known osgEarth target is available")
    endif()

    set(MAP3D_SOURCES
        ${CMAKE_SOURCE_DIR}/src/map3d/MapDataManager.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/Map3DAssetLoader.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/Map3DDiagnosticsFormatter.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/Map3DRuntime.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/Map3DWindow.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/OsgEarthViewWidget.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/TrackSampling.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/Trajectory3DLayer.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/Aircraft3DLayer.cpp
        ${CMAKE_SOURCE_DIR}/src/map3d/AircraftHeading.cpp
    )

    set(MAP3D_HEADERS
        ${CMAKE_SOURCE_DIR}/include/map3d/MapDataManager.h
        ${CMAKE_SOURCE_DIR}/include/map3d/Map3DWindow.h
        ${CMAKE_SOURCE_DIR}/include/map3d/OsgEarthViewWidget.h
        ${CMAKE_SOURCE_DIR}/include/map3d/TrackSampling.h
        ${CMAKE_SOURCE_DIR}/include/map3d/Trajectory3DLayer.h
        ${CMAKE_SOURCE_DIR}/include/map3d/Aircraft3DLayer.h
        ${CMAKE_SOURCE_DIR}/include/map3d/AircraftHeading.h
        ${CMAKE_SOURCE_DIR}/src/map3d/Map3DAssetLoader.h
        ${CMAKE_SOURCE_DIR}/src/map3d/Map3DDiagnosticsFormatter.h
        ${CMAKE_SOURCE_DIR}/src/map3d/Map3DRuntime.h
    )

    add_library(vaporview_map3d_osgearth STATIC
        ${MAP3D_SOURCES}
        ${MAP3D_HEADERS}
    )

    target_include_directories(vaporview_map3d_osgearth
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${OPENSCENEGRAPH_INCLUDE_DIRS}
    )

    target_compile_definitions(vaporview_map3d_osgearth
        PUBLIC
            VAPORVIEW_HAS_OSGEARTH=1
    )

    target_link_libraries(vaporview_map3d_osgearth
        PUBLIC
            vaporview_app_theme
            vaporview_geo_core
            Qt6::Widgets
            Qt6::OpenGLWidgets
            Qt6::Concurrent
            ${OSGEARTH_TARGET}
            ${OPENSCENEGRAPH_LIBRARIES}
    )

    if(WIN32)
        set(VAPORVIEW_OSGEARTH_RUNTIME_BIN_DIR
            "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/bin"
            CACHE PATH
            "Runtime DLL directory for the optional project-local osgEarth install"
        )
        set(VAPORVIEW_OSGEARTH_PLUGIN_DIR
            "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/plugins"
            CACHE PATH
            "OSG plugin directory for the optional project-local osgEarth install"
        )

        set(vaporview_osgearth_runtime_commands)
        set(vaporview_osgearth_runtime_dependencies)
        if(EXISTS "${VAPORVIEW_OSGEARTH_RUNTIME_BIN_DIR}")
            file(GLOB_RECURSE vaporview_osgearth_runtime_files
                CONFIGURE_DEPENDS
                LIST_DIRECTORIES false
                "${VAPORVIEW_OSGEARTH_RUNTIME_BIN_DIR}/*"
            )
            list(APPEND vaporview_osgearth_runtime_dependencies
                ${vaporview_osgearth_runtime_files}
            )
            list(APPEND vaporview_osgearth_runtime_commands
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${VAPORVIEW_OSGEARTH_RUNTIME_BIN_DIR}"
                    "${CMAKE_BINARY_DIR}"
            )
        endif()

        if(EXISTS "${VAPORVIEW_OSGEARTH_PLUGIN_DIR}")
            file(GLOB vaporview_osg_plugin_dirs
                LIST_DIRECTORIES true
                "${VAPORVIEW_OSGEARTH_PLUGIN_DIR}/osgPlugins-*"
            )
            foreach(vaporview_osg_plugin_dir IN LISTS vaporview_osg_plugin_dirs)
                file(GLOB_RECURSE vaporview_osg_plugin_files
                    CONFIGURE_DEPENDS
                    LIST_DIRECTORIES false
                    "${vaporview_osg_plugin_dir}/*"
                )
                list(APPEND vaporview_osgearth_runtime_dependencies
                    ${vaporview_osg_plugin_files}
                )
                get_filename_component(vaporview_osg_plugin_name
                    "${vaporview_osg_plugin_dir}"
                    NAME
                )
                list(APPEND vaporview_osgearth_runtime_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${vaporview_osg_plugin_dir}"
                        "${CMAKE_BINARY_DIR}/${vaporview_osg_plugin_name}"
                )
            endforeach()
        endif()

        foreach(vaporview_osgearth_share_dir IN ITEMS gdal proj proj4)
            if(EXISTS "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/share/${vaporview_osgearth_share_dir}")
                file(GLOB_RECURSE vaporview_osgearth_share_files
                    CONFIGURE_DEPENDS
                    LIST_DIRECTORIES false
                    "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/share/${vaporview_osgearth_share_dir}/*"
                )
                list(APPEND vaporview_osgearth_runtime_dependencies
                    ${vaporview_osgearth_share_files}
                )
                list(APPEND vaporview_osgearth_runtime_commands
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "${CMAKE_BINARY_DIR}/share/${vaporview_osgearth_share_dir}"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${VAPORVIEW_LOCAL_VCPKG_INSTALLED_DIR}/x64-windows/share/${vaporview_osgearth_share_dir}"
                        "${CMAKE_BINARY_DIR}/share/${vaporview_osgearth_share_dir}"
                )
            endif()
        endforeach()

        set(vaporview_default_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_default.earth")
        set(vaporview_dem_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_with_dem.earth")
        set(vaporview_srtm_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_with_srtm.earth")
        set(vaporview_full_local_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_full_local.earth")
        set(vaporview_full_local_srtm_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_full_local_srtm.earth")
        set(vaporview_real3d_local_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_real3d_local.earth")
        set(vaporview_sentinel2_imagery_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_with_sentinel2_imagery.earth")
        set(vaporview_landsat_imagery_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_with_landsat_imagery.earth")
        set(vaporview_openaerialmap_imagery_earth_file "${CMAKE_SOURCE_DIR}/resources/maps/vaporview_with_openaerialmap_imagery.earth")
        set(vaporview_natural_earth_texture
            "${CMAKE_SOURCE_DIR}/resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"
        )
        set(vaporview_natural_earth_vrt
            "${CMAKE_SOURCE_DIR}/resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"
        )
        set(vaporview_natural_earth_raster
            "${CMAKE_SOURCE_DIR}/resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"
        )
        set(vaporview_required_map_resources
            "${vaporview_default_earth_file}"
            "${vaporview_natural_earth_vrt}"
            "${vaporview_natural_earth_raster}"
            "${vaporview_natural_earth_texture}"
        )
        foreach(vaporview_required_map_resource IN LISTS vaporview_required_map_resources)
            if(NOT EXISTS "${vaporview_required_map_resource}")
                message(FATAL_ERROR
                    "Required osgEarth map resource is missing: ${vaporview_required_map_resource}"
                )
            endif()
        endforeach()
        list(APPEND vaporview_osgearth_runtime_dependencies
            ${vaporview_required_map_resources}
        )
        list(APPEND vaporview_osgearth_runtime_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/resources/maps/natural_earth/NE2_50M_SR_W"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${vaporview_default_earth_file}"
                "${CMAKE_BINARY_DIR}/resources/maps/vaporview_default.earth"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${vaporview_natural_earth_vrt}"
                "${CMAKE_BINARY_DIR}/resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${vaporview_natural_earth_raster}"
                "${CMAKE_BINARY_DIR}/resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.tif"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${vaporview_natural_earth_texture}"
                "${CMAKE_BINARY_DIR}/resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W_2048.png"
        )

        set(vaporview_optional_earth_files
            "${vaporview_dem_earth_file}"
            "${vaporview_srtm_earth_file}"
            "${vaporview_full_local_earth_file}"
            "${vaporview_full_local_srtm_earth_file}"
            "${vaporview_real3d_local_earth_file}"
            "${vaporview_sentinel2_imagery_earth_file}"
            "${vaporview_landsat_imagery_earth_file}"
            "${vaporview_openaerialmap_imagery_earth_file}"
        )
        foreach(vaporview_optional_earth_file IN LISTS vaporview_optional_earth_files)
            if(EXISTS "${vaporview_optional_earth_file}")
                get_filename_component(vaporview_optional_earth_name
                    "${vaporview_optional_earth_file}"
                    NAME
                )
                list(APPEND vaporview_osgearth_runtime_dependencies
                    "${vaporview_optional_earth_file}"
                )
                list(APPEND vaporview_osgearth_runtime_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${vaporview_optional_earth_file}"
                        "${CMAKE_BINARY_DIR}/resources/maps/${vaporview_optional_earth_name}"
                )
            else()
                message(STATUS
                    "Optional osgEarth map resource is unavailable: ${vaporview_optional_earth_file}"
                )
            endif()
        endforeach()

        if(vaporview_osgearth_runtime_commands)
            set(vaporview_osgearth_runtime_stamp
                "${CMAKE_BINARY_DIR}/VaporViewOsgEarthRuntime.stamp"
            )
            add_custom_command(
                OUTPUT "${vaporview_osgearth_runtime_stamp}"
                ${vaporview_osgearth_runtime_commands}
                COMMAND ${CMAKE_COMMAND} -E touch "${vaporview_osgearth_runtime_stamp}"
                DEPENDS ${vaporview_osgearth_runtime_dependencies}
                COMMENT "Synchronizing project-local osgEarth runtime"
                VERBATIM
            )
            add_custom_target(VaporViewOsgEarthRuntime
                DEPENDS "${vaporview_osgearth_runtime_stamp}"
            )

            function(vaporview_stage_osgearth_runtime target_name)
                add_dependencies(${target_name} VaporViewOsgEarthRuntime)
            endfunction()
        else()
            function(vaporview_stage_osgearth_runtime target_name)
                message(WARNING
                    "No project-local osgEarth runtime directories found for ${target_name}; "
                    "runtime DLLs must be available on PATH."
                )
            endfunction()
        endif()
    endif()

    add_executable(vaporview_building_tileset
        ${CMAKE_SOURCE_DIR}/tools/BuildingTilesetGenerator.cpp
    )
    target_include_directories(vaporview_building_tileset PRIVATE
        ${OPENSCENEGRAPH_INCLUDE_DIRS}
    )
    target_link_libraries(vaporview_building_tileset PRIVATE
        Qt6::Core
        GDAL::GDAL
        ${OPENSCENEGRAPH_LIBRARIES}
    )
    if(WIN32)
        vaporview_stage_osgearth_runtime(vaporview_building_tileset)
    endif()
endif()
