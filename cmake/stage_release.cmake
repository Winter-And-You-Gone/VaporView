if(NOT DEFINED SOURCE_DIR OR NOT DEFINED TARGET_FILE OR NOT DEFINED STAGE_DIR)
    message(FATAL_ERROR "stage_release.cmake requires SOURCE_DIR, TARGET_FILE, and STAGE_DIR")
endif()

file(MAKE_DIRECTORY "${STAGE_DIR}")
file(COPY "${TARGET_FILE}" DESTINATION "${STAGE_DIR}")

file(GLOB runtime_dlls "${SOURCE_DIR}/*.dll")
foreach(runtime_dll IN LISTS runtime_dlls)
    file(COPY "${runtime_dll}" DESTINATION "${STAGE_DIR}")
endforeach()

set(plugin_dirs
    generic
    iconengines
    imageformats
    networkinformation
    platforms
    styles
    tls
)

foreach(plugin_dir IN LISTS plugin_dirs)
    if(EXISTS "${SOURCE_DIR}/${plugin_dir}")
        file(REMOVE_RECURSE "${STAGE_DIR}/${plugin_dir}")
        file(COPY "${SOURCE_DIR}/${plugin_dir}" DESTINATION "${STAGE_DIR}")
    endif()
endforeach()

if(EXISTS "${SOURCE_DIR}/resources")
    file(REMOVE_RECURSE "${STAGE_DIR}/resources")
    file(COPY "${SOURCE_DIR}/resources" DESTINATION "${STAGE_DIR}")
endif()
