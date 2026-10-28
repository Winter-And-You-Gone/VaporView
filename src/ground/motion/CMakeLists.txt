add_library(vaporview_ground_motion STATIC
    EmFwc3Session.cpp EmFwc3Session.h
    MotionControlPage.cpp MotionControlPage.h
)
target_include_directories(vaporview_ground_motion PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(vaporview_ground_motion PUBLIC Qt6::Widgets PRIVATE
    Qt6::SerialPort vaporview_app_theme vaporview_app_config)

# Vendor binaries stay outside Git. A configured SDK is staged and installed intact.
set(VAPORVIEW_EMFWC3_SDK_DIR "" CACHE PATH "LBTEK EM-FWC3 x64 SDK runtime directory")
if(WIN32 AND VAPORVIEW_EMFWC3_SDK_DIR)
    set(emfwc3_runtime_files)
    foreach(dll LB_Control_Library_x64.dll lubonCore.dll libusb-1.0.dll)
        if(NOT EXISTS "${VAPORVIEW_EMFWC3_SDK_DIR}/${dll}")
            message(FATAL_ERROR "EM-FWC3 SDK runtime missing: ${dll}")
        endif()
        list(APPEND emfwc3_runtime_files "${VAPORVIEW_EMFWC3_SDK_DIR}/${dll}")
    endforeach()
    add_custom_target(VaporViewEmFwc3Runtime
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/emfwc3"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${emfwc3_runtime_files} "${CMAKE_BINARY_DIR}/emfwc3"
        VERBATIM)
    add_dependencies(vaporview_ground_motion VaporViewEmFwc3Runtime)
    install(FILES ${emfwc3_runtime_files} DESTINATION bin/emfwc3)
endif()
