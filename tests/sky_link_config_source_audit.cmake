file(GLOB_RECURSE VAPORVIEW_GROUND_MAIN_SOURCES
     "${VAPORVIEW_SOURCE_DIR}/src/ground/main/*.cpp"
     "${VAPORVIEW_SOURCE_DIR}/src/ground/main/*.h")

set(VAPORVIEW_GROUND_MAIN_TEXT "")
foreach(source_file IN LISTS VAPORVIEW_GROUND_MAIN_SOURCES)
    file(READ "${source_file}" source_text)
    string(APPEND VAPORVIEW_GROUND_MAIN_TEXT "\n${source_text}")
endforeach()

# Keep each symbol split so a repository-wide grep does not count this audit's
# own input data as a production reference.
set(forbidden_home_sky_symbol_parts
    sky_telemetry_row _widget_
    sky_telemetry_transport _combo_
    sky_telemetry_port _combo_
    sky_telemetry_baud _combo_
    sky_telemetry_tcp_host _edit_
    sky_telemetry_tcp_port _spin_)
while(forbidden_home_sky_symbol_parts)
    list(POP_FRONT forbidden_home_sky_symbol_parts symbol_prefix symbol_suffix)
    string(CONCAT forbidden_symbol "${symbol_prefix}" "${symbol_suffix}")
    string(FIND "${VAPORVIEW_GROUND_MAIN_TEXT}" "${forbidden_symbol}" symbol_position)
    if(NOT symbol_position EQUAL -1)
        message(FATAL_ERROR
            "Legacy home Sky Link widget symbol remains: ${forbidden_symbol}")
    endif()
endwhile()

file(READ
     "${VAPORVIEW_SOURCE_DIR}/src/ground/main/GroundMainWindowConnection.cpp"
     VAPORVIEW_CONNECTION_TEXT)
foreach(required_connection_token IN ITEMS
        "remote_sky_link_config_"
        "skyLink.tcpHost"
        "skyLink.tcpPort"
        "skyLink.serialPort"
        "skyLink.serialBaudRate")
    string(FIND "${VAPORVIEW_CONNECTION_TEXT}"
                "${required_connection_token}"
                connection_token_position)
    if(connection_token_position EQUAL -1)
        message(FATAL_ERROR
            "Remote connection path no longer reads RemoteSkyLinkConfig field: ${required_connection_token}")
    endif()
endforeach()

message(STATUS "Sky Link configuration source audit passed")
