file(GLOB_RECURSE VAPORVIEW_GROUND_MAIN_SOURCES
     "${VAPORVIEW_SOURCE_DIR}/src/ground/main/*.cpp"
     "${VAPORVIEW_SOURCE_DIR}/src/ground/main/*.h")

set(VAPORVIEW_GROUND_MAIN_TEXT "")
foreach(source_file IN LISTS VAPORVIEW_GROUND_MAIN_SOURCES)
    file(READ "${source_file}" source_text)
    string(APPEND VAPORVIEW_GROUND_MAIN_TEXT "\n${source_text}")
endforeach()

set(forbidden_symbol_parts
    mirrorCombo ToHome
    syncDeviceConfig PageFromHome
    kDeviceConfig LocalMirrorOnlyProperty
    config_form _widget
    epsilon_port _combo_
    epsilon_baud _combo_
    ptb_port _combo_
    ptb_baud _combo_
    hmp_port _combo_
    hmp_baud _combo_
    lidar_port _combo_
    lidar_baud _combo_
    temperature_port _combo_
    temperature_baud _combo_)
while(forbidden_symbol_parts)
    list(POP_FRONT forbidden_symbol_parts symbol_prefix symbol_suffix)
    string(CONCAT forbidden_symbol "${symbol_prefix}" "${symbol_suffix}")
    string(FIND "${VAPORVIEW_GROUND_MAIN_TEXT}" "${forbidden_symbol}" symbol_position)
    if(NOT symbol_position EQUAL -1)
        message(FATAL_ERROR
            "Legacy hidden local-device UI symbol remains: ${forbidden_symbol}")
    endif()
endwhile()

file(READ
     "${VAPORVIEW_SOURCE_DIR}/src/ground/main/GroundMainWindowConnection.cpp"
     VAPORVIEW_CONNECTION_TEXT)
if(NOT VAPORVIEW_CONNECTION_TEXT MATCHES "local_device_config_")
    message(FATAL_ERROR "Local connection path no longer reads LocalDeviceConfig")
endif()

message(STATUS "Local-device configuration source audit passed")
