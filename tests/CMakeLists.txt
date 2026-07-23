set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}")
endforeach()

add_executable(trajectory_buffer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/trajectory_buffer_test.cpp
)
target_link_libraries(trajectory_buffer_test PRIVATE vaporview_geo_core)
add_test(NAME trajectory_buffer_test COMMAND trajectory_buffer_test)

add_executable(trajectory_quality_test
    ${CMAKE_CURRENT_SOURCE_DIR}/trajectory_quality_test.cpp
)
target_link_libraries(trajectory_quality_test PRIVATE vaporview_geo_core)
add_test(NAME trajectory_quality_test COMMAND trajectory_quality_test)

add_executable(trajectory_replay_test
    ${CMAKE_CURRENT_SOURCE_DIR}/trajectory_replay_test.cpp
)
target_link_libraries(trajectory_replay_test PRIVATE vaporview_geo_core)
add_test(NAME trajectory_replay_test COMMAND trajectory_replay_test)

add_executable(session_track_reader_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_track_reader_test.cpp
)
target_link_libraries(session_track_reader_test PRIVATE vaporview_geo_core)
add_test(NAME session_track_reader_test COMMAND session_track_reader_test)

add_executable(coordinate_transform_test
    ${CMAKE_CURRENT_SOURCE_DIR}/coordinate_transform_test.cpp
)
target_link_libraries(coordinate_transform_test PRIVATE vaporview_geo_core)
add_test(NAME coordinate_transform_test COMMAND coordinate_transform_test)

add_executable(height_reference_test
    ${CMAKE_CURRENT_SOURCE_DIR}/height_reference_test.cpp
)
target_link_libraries(height_reference_test PRIVATE vaporview_geo_core)
add_test(NAME height_reference_test COMMAND height_reference_test)

add_executable(map_data_script_test
    ${CMAKE_CURRENT_SOURCE_DIR}/map_data_script_test.cpp
)
target_compile_definitions(map_data_script_test PRIVATE
    VAPORVIEW_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(map_data_script_test PRIVATE Qt6::Core)
add_test(NAME map_data_script_test COMMAND map_data_script_test)
set_tests_properties(map_data_script_test PROPERTIES
    SKIP_RETURN_CODE 77
)

add_executable(tcp_wave_encoding_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_wave_encoding_test.cpp
)
target_include_directories(tcp_wave_encoding_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(tcp_wave_encoding_test PRIVATE vaporview_sky_core)
add_test(NAME tcp_wave_encoding_test COMMAND tcp_wave_encoding_test)

add_executable(tcp_wave_panel_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_wave_panel_test.cpp
)
target_include_directories(tcp_wave_panel_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(tcp_wave_panel_test PRIVATE
    VAPORVIEW_MAIN_WINDOW_TESTING=1
)
target_link_libraries(tcp_wave_panel_test PRIVATE vaporview_ground_wave)
add_dependencies(tcp_wave_panel_test VaporViewRuntimeResources)
add_test(NAME tcp_wave_panel_test COMMAND tcp_wave_panel_test)

add_executable(temperature_controller_protocol_test
    ${CMAKE_CURRENT_SOURCE_DIR}/temperature_controller_protocol_test.cpp
)
target_include_directories(temperature_controller_protocol_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(temperature_controller_protocol_test PRIVATE vaporview_sky_core)
add_test(NAME temperature_controller_protocol_test COMMAND temperature_controller_protocol_test)

add_executable(telemetry_codec_test
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_codec_test.cpp
)
target_include_directories(telemetry_codec_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(telemetry_codec_test PRIVATE vaporview_protocol)
add_test(NAME telemetry_codec_test COMMAND telemetry_codec_test)

add_executable(telemetry_tcp_link_test
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_tcp_link_test.cpp
)
target_include_directories(telemetry_tcp_link_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(telemetry_tcp_link_test PRIVATE vaporview_sky_core)
add_test(NAME telemetry_tcp_link_test COMMAND telemetry_tcp_link_test)

add_executable(sky_device_config_dialog_test
    ${CMAKE_CURRENT_SOURCE_DIR}/sky_device_config_dialog_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ground/widgets/SkyDeviceConfigDialog.h
)
target_include_directories(sky_device_config_dialog_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(sky_device_config_dialog_test PRIVATE
    vaporview_ground_ui_support
)
add_test(NAME sky_device_config_dialog_test COMMAND sky_device_config_dialog_test)

add_executable(sky_session_recorder_test
    ${CMAKE_CURRENT_SOURCE_DIR}/sky_session_recorder_test.cpp
)
target_include_directories(sky_session_recorder_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(sky_session_recorder_test PRIVATE vaporview_sky_core)
add_test(NAME sky_session_recorder_test COMMAND sky_session_recorder_test)

add_executable(sky_local_ipc_client_test
    ${CMAKE_CURRENT_SOURCE_DIR}/sky_local_ipc_client_test.cpp
)
target_include_directories(sky_local_ipc_client_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(sky_local_ipc_client_test PRIVATE vaporview_sky_tui)
add_test(NAME sky_local_ipc_client_test COMMAND sky_local_ipc_client_test)

add_executable(visual_text_label_test
    ${CMAKE_CURRENT_SOURCE_DIR}/visual_text_label_test.cpp
)
target_include_directories(visual_text_label_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(visual_text_label_test PRIVATE Qt6::Widgets)
add_test(NAME visual_text_label_test COMMAND visual_text_label_test)

add_executable(session_time_format_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_time_format_test.cpp
)
target_link_libraries(session_time_format_test PRIVATE vaporview_ground_session_core)
add_test(NAME session_time_format_test COMMAND session_time_format_test)

add_executable(device_rate_policy_test
    ${CMAKE_CURRENT_SOURCE_DIR}/device_rate_policy_test.cpp
)
target_link_libraries(device_rate_policy_test PRIVATE vaporview_ground_device_policy)
add_test(NAME device_rate_policy_test COMMAND device_rate_policy_test)

add_executable(rtk_serial_topology_test
    ${CMAKE_CURRENT_SOURCE_DIR}/rtk_serial_topology_test.cpp
)
target_link_libraries(rtk_serial_topology_test PRIVATE vaporview_ground_rtk)
add_test(NAME rtk_serial_topology_test COMMAND rtk_serial_topology_test)

add_executable(rtk_config_dialog_test
    ${CMAKE_CURRENT_SOURCE_DIR}/rtk_config_dialog_test.cpp
)
target_link_libraries(rtk_config_dialog_test PRIVATE vaporview_ground_rtk)
add_test(NAME rtk_config_dialog_test COMMAND rtk_config_dialog_test)
set_tests_properties(rtk_config_dialog_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")

add_executable(bounded_lru_cache_test
    ${CMAKE_CURRENT_SOURCE_DIR}/bounded_lru_cache_test.cpp
)
target_include_directories(bounded_lru_cache_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(bounded_lru_cache_test PRIVATE Qt6::Core)
add_test(NAME bounded_lru_cache_test COMMAND bounded_lru_cache_test)

add_executable(bounded_byte_queue_test
    ${CMAKE_CURRENT_SOURCE_DIR}/bounded_byte_queue_test.cpp
)
target_include_directories(bounded_byte_queue_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(bounded_byte_queue_test PRIVATE Qt6::Core)
add_test(NAME bounded_byte_queue_test COMMAND bounded_byte_queue_test)

add_executable(temperature_command_state_test
    ${CMAKE_CURRENT_SOURCE_DIR}/temperature_command_state_test.cpp
)
target_link_libraries(temperature_command_state_test PRIVATE
    vaporview_ground_temperature_command_state
)
add_test(NAME temperature_command_state_test COMMAND temperature_command_state_test)

add_executable(epsilon_protocol_test
    ${CMAKE_CURRENT_SOURCE_DIR}/epsilon_protocol_test.cpp
)
target_link_libraries(epsilon_protocol_test PRIVATE vaporview_sky_core vaporview_geo_core)
add_test(NAME epsilon_protocol_test COMMAND epsilon_protocol_test)

add_executable(device_panel_coordinator_test
    ${CMAKE_CURRENT_SOURCE_DIR}/device_panel_coordinator_test.cpp
)
target_link_libraries(device_panel_coordinator_test PRIVATE vaporview_ground_widgets)
add_test(NAME device_panel_coordinator_test COMMAND device_panel_coordinator_test)

add_executable(session_core_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_core_test.cpp
)
target_link_libraries(session_core_test PRIVATE vaporview_ground_session_core)
add_test(NAME session_core_test COMMAND session_core_test)

add_executable(session_format_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_format_test.cpp
)
target_link_libraries(session_format_test PRIVATE vaporview_session_format)
add_test(NAME session_format_test COMMAND session_format_test)

add_executable(session_path_resolver_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_path_resolver_test.cpp
)
target_link_libraries(session_path_resolver_test PRIVATE vaporview_session_format)
add_test(NAME session_path_resolver_test COMMAND session_path_resolver_test)

add_executable(session_timeline_model_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_timeline_model_test.cpp
)
target_link_libraries(session_timeline_model_test PRIVATE vaporview_ground_session_core)
add_test(NAME session_timeline_model_test COMMAND session_timeline_model_test)

add_executable(session_export_trajectory_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_export_trajectory_test.cpp
)
target_link_libraries(session_export_trajectory_test PRIVATE vaporview_ground_session_core)
add_test(NAME session_export_trajectory_test COMMAND session_export_trajectory_test)

add_executable(session_viewer_components_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_viewer_components_test.cpp
)
target_include_directories(session_viewer_components_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(session_viewer_components_test PRIVATE
    vaporview_ground_session_ui
    vaporview_ground_trajectory
)
add_dependencies(session_viewer_components_test VaporViewRuntimeResources)
add_test(NAME session_viewer_components_test COMMAND session_viewer_components_test)

add_executable(ground_recording_service_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ground_recording_service_test.cpp
)
target_link_libraries(ground_recording_service_test PRIVATE vaporview_ground_session_core)
add_test(NAME ground_recording_service_test COMMAND ground_recording_service_test)

add_executable(recording_schedule_controller_test
    ${CMAKE_CURRENT_SOURCE_DIR}/recording_schedule_controller_test.cpp
)
target_link_libraries(recording_schedule_controller_test PRIVATE vaporview_ground_session_core)
add_test(NAME recording_schedule_controller_test COMMAND recording_schedule_controller_test)

add_executable(remote_telemetry_decoder_test
    ${CMAKE_CURRENT_SOURCE_DIR}/remote_telemetry_decoder_test.cpp
)
target_include_directories(remote_telemetry_decoder_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(remote_telemetry_decoder_test PRIVATE vaporview_ground_device_core)
add_test(NAME remote_telemetry_decoder_test COMMAND remote_telemetry_decoder_test)

add_executable(remote_telemetry_state_test
    ${CMAKE_CURRENT_SOURCE_DIR}/remote_telemetry_state_test.cpp
)
target_link_libraries(remote_telemetry_state_test PRIVATE vaporview_ground_device_core)
add_test(NAME remote_telemetry_state_test COMMAND remote_telemetry_state_test)

add_executable(local_device_connection_controller_test
    ${CMAKE_CURRENT_SOURCE_DIR}/local_device_connection_controller_test.cpp
)
target_link_libraries(local_device_connection_controller_test PRIVATE vaporview_ground_device_core)
add_test(NAME local_device_connection_controller_test COMMAND local_device_connection_controller_test)

add_executable(imu_configuration_service_test
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_configuration_service_test.cpp
)
target_link_libraries(imu_configuration_service_test PRIVATE vaporview_ground_device_core)
add_test(NAME imu_configuration_service_test COMMAND imu_configuration_service_test)

add_executable(serial_port_detection_service_test
    ${CMAKE_CURRENT_SOURCE_DIR}/serial_port_detection_service_test.cpp
)
target_link_libraries(serial_port_detection_service_test PRIVATE vaporview_ground_device_core)
add_test(NAME serial_port_detection_service_test COMMAND serial_port_detection_service_test)

find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(main_window_layout_test
    ${CMAKE_CURRENT_SOURCE_DIR}/main_window_layout_test.cpp
)
target_include_directories(main_window_layout_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(main_window_layout_test PRIVATE
    VAPORVIEW_MAIN_WINDOW_TESTING=1
)
target_link_libraries(main_window_layout_test PRIVATE vaporview_main_window Qt6::Test)
add_dependencies(main_window_layout_test VaporViewRuntimeResources)
if(VAPORVIEW_ENABLE_OSGEARTH AND WIN32)
    vaporview_stage_osgearth_runtime(main_window_layout_test)
endif()
add_custom_command(TARGET main_window_layout_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::Test>
        $<TARGET_FILE_DIR:main_window_layout_test>
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:main_window_layout_test>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
        $<TARGET_FILE_DIR:main_window_layout_test>/resources/modern_style.qss
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
        $<TARGET_FILE_DIR:main_window_layout_test>/resources/combo_arrow_down.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
        $<TARGET_FILE_DIR:main_window_layout_test>/resources/combo_arrow_up.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/lucide
        $<TARGET_FILE_DIR:main_window_layout_test>/resources/lucide
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO
        $<TARGET_FILE_DIR:main_window_layout_test>/resources/VaproViewLOGO
    COMMENT "Copying resources for main_window_layout_test"
)
add_test(NAME main_window_layout_test COMMAND main_window_layout_test)

add_executable(main_window_session_viewer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/main_window_session_viewer_test.cpp
)
target_include_directories(main_window_session_viewer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(main_window_session_viewer_test PRIVATE vaporview_main_window)
add_dependencies(main_window_session_viewer_test VaporViewRuntimeResources)
if(VAPORVIEW_ENABLE_OSGEARTH AND WIN32)
    vaporview_stage_osgearth_runtime(main_window_session_viewer_test)
endif()
add_test(NAME main_window_session_viewer_test COMMAND main_window_session_viewer_test)

add_executable(session_viewer_theme_test
    ${CMAKE_CURRENT_SOURCE_DIR}/session_viewer_theme_test.cpp
)
target_include_directories(session_viewer_theme_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(session_viewer_theme_test PRIVATE
    vaporview_app_theme
    vaporview_ground_session_ui
    vaporview_ground_trajectory
    vaporview_ground_wave
)
add_dependencies(session_viewer_theme_test VaporViewRuntimeResources)
add_test(NAME session_viewer_io_test
    COMMAND session_viewer_theme_test --group io)
add_test(NAME session_viewer_window_state_test
    COMMAND session_viewer_theme_test --group window-state)
add_test(NAME session_viewer_trajectory_test
    COMMAND session_viewer_theme_test --group trajectory)
add_test(NAME session_viewer_theme_test
    COMMAND session_viewer_theme_test --group theme)

if(VAPORVIEW_ENABLE_OSGEARTH)
    add_executable(map_data_manager_test
        ${CMAKE_CURRENT_SOURCE_DIR}/map_data_manager_test.cpp
    )
    target_include_directories(map_data_manager_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(map_data_manager_test PRIVATE vaporview_map3d_osgearth)
    add_test(NAME map_data_manager_test COMMAND map_data_manager_test)

    add_executable(trajectory_3d_layer_test
        ${CMAKE_CURRENT_SOURCE_DIR}/trajectory_3d_layer_test.cpp
    )
    target_include_directories(trajectory_3d_layer_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(trajectory_3d_layer_test PRIVATE vaporview_map3d_osgearth)
    add_test(NAME trajectory_3d_layer_test COMMAND trajectory_3d_layer_test)

    add_executable(aircraft_3d_layer_test
        ${CMAKE_CURRENT_SOURCE_DIR}/aircraft_3d_layer_test.cpp
    )
    target_include_directories(aircraft_3d_layer_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(aircraft_3d_layer_test PRIVATE vaporview_map3d_osgearth)
    add_test(NAME aircraft_3d_layer_test COMMAND aircraft_3d_layer_test)

    add_executable(aircraft_heading_test
        ${CMAKE_CURRENT_SOURCE_DIR}/aircraft_heading_test.cpp
    )
    target_include_directories(aircraft_heading_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(aircraft_heading_test PRIVATE vaporview_map3d_osgearth)
    add_test(NAME aircraft_heading_test COMMAND aircraft_heading_test)

    add_executable(map3d_window_smoke_test
        ${CMAKE_CURRENT_SOURCE_DIR}/map3d_window_smoke_test.cpp
    )
    target_include_directories(map3d_window_smoke_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(map3d_window_smoke_test PRIVATE vaporview_map3d_osgearth)
    if(WIN32)
        vaporview_stage_osgearth_runtime(map3d_window_smoke_test)
    endif()
    add_test(NAME map3d_window_smoke_test COMMAND map3d_window_smoke_test)

    add_executable(map3d_real_data_load_test
        ${CMAKE_CURRENT_SOURCE_DIR}/map3d_real_data_load_test.cpp
    )
    target_include_directories(map3d_real_data_load_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_definitions(map3d_real_data_load_test PRIVATE
        VAPORVIEW_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(map3d_real_data_load_test PRIVATE vaporview_map3d_osgearth)
    if(WIN32)
        vaporview_stage_osgearth_runtime(map3d_real_data_load_test)
    endif()
    add_test(NAME map3d_real_data_load_test COMMAND map3d_real_data_load_test)
    set_tests_properties(map3d_real_data_load_test PROPERTIES
        SKIP_RETURN_CODE 77
    )

    add_executable(map3d_real_window_close_test
        ${CMAKE_CURRENT_SOURCE_DIR}/map3d_real_window_close_test.cpp
    )
    target_include_directories(map3d_real_window_close_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_definitions(map3d_real_window_close_test PRIVATE
        VAPORVIEW_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(map3d_real_window_close_test PRIVATE vaporview_map3d_osgearth)
    if(WIN32)
        vaporview_stage_osgearth_runtime(map3d_real_window_close_test)
    endif()
    add_test(NAME map3d_real_window_close_test COMMAND map3d_real_window_close_test)
    set_tests_properties(map3d_real_window_close_test PROPERTIES
        SKIP_RETURN_CODE 77
    )

    add_executable(main_window_map3d_live_test
        ${CMAKE_CURRENT_SOURCE_DIR}/main_window_map3d_live_test.cpp
    )
    target_include_directories(main_window_map3d_live_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(main_window_map3d_live_test PRIVATE
        VAPORVIEW_MAIN_WINDOW_TESTING=1
    )
    target_link_libraries(main_window_map3d_live_test PRIVATE vaporview_main_window)
    add_dependencies(main_window_map3d_live_test VaporViewRuntimeResources)
    if(WIN32)
        vaporview_stage_osgearth_runtime(main_window_map3d_live_test)
    endif()
    add_test(NAME main_window_map3d_live_test COMMAND main_window_map3d_live_test)

    add_executable(main_window_map3d_real_close_test
        ${CMAKE_CURRENT_SOURCE_DIR}/main_window_map3d_real_close_test.cpp
    )
    target_include_directories(main_window_map3d_real_close_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_definitions(main_window_map3d_real_close_test PRIVATE
        VAPORVIEW_MAIN_WINDOW_TESTING=1
        VAPORVIEW_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(main_window_map3d_real_close_test PRIVATE
        vaporview_main_window
        vaporview_map3d_osgearth
    )
    add_dependencies(main_window_map3d_real_close_test VaporViewRuntimeResources)
    if(WIN32)
        vaporview_stage_osgearth_runtime(main_window_map3d_real_close_test)
    endif()
    add_test(NAME main_window_map3d_real_close_test COMMAND main_window_map3d_real_close_test)
    set_tests_properties(main_window_map3d_real_close_test PROPERTIES
        SKIP_RETURN_CODE 77
    )
endif()

add_executable(vaporview_startup_test
    ${CMAKE_CURRENT_SOURCE_DIR}/vaporview_startup_test.cpp
)
target_link_libraries(vaporview_startup_test PRIVATE Qt6::Core)
add_test(NAME vaporview_startup_test COMMAND vaporview_startup_test)

set(VAPORVIEW_FAST_UNIT_TESTS
    trajectory_buffer_test
    trajectory_quality_test
    trajectory_replay_test
    session_track_reader_test
    coordinate_transform_test
    height_reference_test
    tcp_wave_encoding_test
    temperature_controller_protocol_test
    telemetry_codec_test
    telemetry_tcp_link_test
    sky_session_recorder_test
    sky_local_ipc_client_test
    visual_text_label_test
    session_time_format_test
    device_rate_policy_test
    rtk_serial_topology_test
    rtk_config_dialog_test
    bounded_lru_cache_test
    bounded_byte_queue_test
    temperature_command_state_test
    epsilon_protocol_test
    session_format_test
    session_path_resolver_test
    session_timeline_model_test
    session_export_trajectory_test
    ground_recording_service_test
    recording_schedule_controller_test
    remote_telemetry_decoder_test
    remote_telemetry_state_test
    local_device_connection_controller_test
    imu_configuration_service_test
    serial_port_detection_service_test
)
set_tests_properties(${VAPORVIEW_FAST_UNIT_TESTS} PROPERTIES TIMEOUT 30)
set_property(TEST ${VAPORVIEW_FAST_UNIT_TESTS} PROPERTY LABELS fast unit)

set(VAPORVIEW_FAST_INTEGRATION_TESTS
    map_data_script_test
    device_panel_coordinator_test
    tcp_wave_panel_test
    sky_device_config_dialog_test
    vaporview_startup_test
)
set_tests_properties(${VAPORVIEW_FAST_INTEGRATION_TESTS} PROPERTIES TIMEOUT 60)
set_property(TEST ${VAPORVIEW_FAST_INTEGRATION_TESTS} PROPERTY LABELS fast integration)

set(VAPORVIEW_SLOW_UI_TESTS
    main_window_layout_test
    main_window_session_viewer_test
    session_viewer_io_test
    session_viewer_window_state_test
    session_viewer_trajectory_test
    session_viewer_theme_test
    session_viewer_components_test
)
set_tests_properties(${VAPORVIEW_SLOW_UI_TESTS} PROPERTIES
    TIMEOUT 120
    RUN_SERIAL TRUE
)
set_property(TEST ${VAPORVIEW_SLOW_UI_TESTS} PROPERTY LABELS integration slow ui)

set_property(TEST map_data_script_test APPEND PROPERTY LABELS script)
set_property(TEST tcp_wave_encoding_test tcp_wave_panel_test APPEND PROPERTY LABELS tcp-wave)
set_property(TEST device_panel_coordinator_test APPEND PROPERTY LABELS ground-widgets)
set_property(TEST
    temperature_controller_protocol_test
    epsilon_protocol_test
    telemetry_codec_test
    telemetry_tcp_link_test
    APPEND PROPERTY LABELS protocol
)
set_property(TEST
    main_window_layout_test
    session_viewer_io_test
    session_viewer_window_state_test
    session_viewer_trajectory_test
    session_viewer_theme_test
    session_viewer_components_test
    vaporview_startup_test
    APPEND PROPERTY LABELS ui
)
set_property(TEST main_window_layout_test APPEND PROPERTY LABELS main-window layout)
set_property(TEST main_window_session_viewer_test APPEND PROPERTY LABELS main-window session-viewer)
set_property(TEST session_viewer_io_test APPEND PROPERTY LABELS session-viewer io)
set_property(TEST session_viewer_window_state_test APPEND PROPERTY LABELS session-viewer window-state)
set_property(TEST session_viewer_trajectory_test APPEND PROPERTY LABELS session-viewer trajectory)
set_property(TEST session_viewer_theme_test APPEND PROPERTY LABELS session-viewer theme)
set_property(TEST session_export_trajectory_test APPEND PROPERTY LABELS session-viewer export trajectory)
set_property(TEST session_viewer_components_test APPEND PROPERTY LABELS session-viewer components map)

if(VAPORVIEW_ENABLE_OSGEARTH)
    set(VAPORVIEW_FAST_3D_UNIT_TESTS
        map_data_manager_test
        trajectory_3d_layer_test
        aircraft_3d_layer_test
        aircraft_heading_test
    )
    set_tests_properties(${VAPORVIEW_FAST_3D_UNIT_TESTS} PROPERTIES TIMEOUT 60)
    set_property(TEST ${VAPORVIEW_FAST_3D_UNIT_TESTS} PROPERTY LABELS 3d fast unit)

    set(VAPORVIEW_FAST_3D_INTEGRATION_TESTS
        map3d_window_smoke_test
    )
    set_tests_properties(${VAPORVIEW_FAST_3D_INTEGRATION_TESTS} PROPERTIES
        TIMEOUT 90
        RUN_SERIAL TRUE
    )
    set_property(TEST ${VAPORVIEW_FAST_3D_INTEGRATION_TESTS} PROPERTY LABELS 3d fast integration ui)

    set_tests_properties(main_window_map3d_live_test PROPERTIES
        TIMEOUT 90
        RUN_SERIAL TRUE
    )
    set_property(TEST main_window_map3d_live_test PROPERTY LABELS 3d integration slow ui)

    set(VAPORVIEW_REAL_3D_TESTS
        map3d_real_data_load_test
        map3d_real_window_close_test
        main_window_map3d_real_close_test
    )
    set_tests_properties(${VAPORVIEW_REAL_3D_TESTS} PROPERTIES
        TIMEOUT 180
        RUN_SERIAL TRUE
    )
    set_property(TEST ${VAPORVIEW_REAL_3D_TESTS} PROPERTY LABELS 3d real-data slow ui)
endif()
