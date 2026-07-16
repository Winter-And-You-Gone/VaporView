if(TARGET vaporview_map3d_osgearth)
    add_library(vaporview_ground_map STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Map3DController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Map3DController.h
    )

    target_include_directories(vaporview_ground_map PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(vaporview_ground_map PUBLIC
        Qt6::Core
    )

    target_link_libraries(vaporview_ground_map PRIVATE
        vaporview_map3d_osgearth
        vaporview_geo_core
        Qt6::Widgets
    )
endif()
