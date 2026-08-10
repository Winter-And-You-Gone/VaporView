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


add_executable(trajectory_heatmap_test
    ${CMAKE_CURRENT_SOURCE_DIR}/trajectory_heatmap_test.cpp
)
target_link_libraries(trajectory_heatmap_test PRIVATE vaporview_geo_core)
add_test(NAME trajectory_heatmap_test COMMAND trajectory_heatmap_test)

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

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME logging_language_audit_test
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/scripts/audit_logging_language.py
                --root ${CMAKE_SOURCE_DIR}
                --self-test
    )
    add_test(NAME logging_event_catalog_audit_test
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/scripts/audit_logging_events.py
                --root ${CMAKE_SOURCE_DIR}
                --self-test
    )
    add_test(NAME logging_legacy_audit_test
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/scripts/audit_legacy_logging.py
                --root ${CMAKE_SOURCE_DIR}
                --self-test
    )
    set_tests_properties(logging_language_audit_test PROPERTIES
        TIMEOUT 30
        LABELS "fast;unit;logging"
    )
    set_tests_properties(logging_event_catalog_audit_test PROPERTIES
        TIMEOUT 30
        LABELS "fast;unit;logging"
    )
    set_tests_properties(logging_legacy_audit_test PROPERTIES
        TIMEOUT 30
        LABELS "fast;unit;logging"
    )
endif()

add_executable(ui_log_model_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_log_model_test.cpp
)
target_include_directories(ui_log_model_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_log_model_test PRIVATE vaporview_main_window)
add_test(NAME ui_log_model_test COMMAND ui_log_model_test)

add_executable(single_level_popup_menu_test
    ${CMAKE_CURRENT_SOURCE_DIR}/single_level_popup_menu_test.cpp
)
target_include_directories(single_level_popup_menu_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(single_level_popup_menu_test PRIVATE vaporview_app_theme)
add_test(NAME single_level_popup_menu_test COMMAND single_level_popup_menu_test)
set_tests_properties(ui_log_model_test PROPERTIES
    TIMEOUT 30
    LABELS "fast;unit;logging;ui"
)

add_executable(main_window_log_panel_test
    ${CMAKE_CURRENT_SOURCE_DIR}/main_window_log_panel_test.cpp
)
target_include_directories(main_window_log_panel_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(main_window_log_panel_test PRIVATE
    VAPORVIEW_MAIN_WINDOW_TESTING=1
)
target_link_libraries(main_window_log_panel_test PRIVATE
    vaporview_main_window
    Qt6::Widgets
)
add_dependencies(main_window_log_panel_test VaporViewRuntimeResources)
add_test(NAME main_window_log_panel_test COMMAND main_window_log_panel_test)
set_tests_properties(main_window_log_panel_test PROPERTIES
    TIMEOUT 60
    LABELS "fast;integration;logging;ui;main-window"
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
target_link_libraries(tcp_wave_panel_test PRIVATE vaporview_ground_wave Qt6::Network)
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

add_executable(ai8_temperature_controller_protocol_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ai8_temperature_controller_protocol_test.cpp
)
target_include_directories(ai8_temperature_controller_protocol_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ai8_temperature_controller_protocol_test PRIVATE vaporview_sky_core)
add_test(NAME ai8_temperature_controller_protocol_test COMMAND ai8_temperature_controller_protocol_test)

add_executable(ai8_temperature_controller_collector_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ai8_temperature_controller_collector_test.cpp
)
target_include_directories(ai8_temperature_controller_collector_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ai8_temperature_controller_collector_test PRIVATE vaporview_sky_core)
add_test(NAME ai8_temperature_controller_collector_test COMMAND ai8_temperature_controller_collector_test)

add_executable(ai8_temperature_controller_panel_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ai8_temperature_controller_panel_test.cpp
)
target_include_directories(ai8_temperature_controller_panel_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ai8_temperature_controller_panel_test PRIVATE
    vaporview_ground_widgets
    vaporview_sky_core
)
add_dependencies(ai8_temperature_controller_panel_test VaporViewRuntimeResources)
add_test(NAME ai8_temperature_controller_panel_test COMMAND ai8_temperature_controller_panel_test)

add_executable(telemetry_codec_test
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_codec_test.cpp
)
target_include_directories(telemetry_codec_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(telemetry_codec_test PRIVATE vaporview_protocol)
add_test(NAME telemetry_codec_test COMMAND telemetry_codec_test)

add_executable(log_service_test
    ${CMAKE_CURRENT_SOURCE_DIR}/log_service_test.cpp
)
target_include_directories(log_service_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/shared
)
target_link_libraries(log_service_test PRIVATE
    vaporview_logging
    vaporview_protocol
    Qt6::Core
)
add_test(NAME log_service_test COMMAND log_service_test)
set_tests_properties(log_service_test PROPERTIES TIMEOUT 90)

add_executable(lifecycle_breadcrumb_test
    ${CMAKE_CURRENT_SOURCE_DIR}/lifecycle_breadcrumb_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/LifecycleBreadcrumb.cpp
)
target_compile_definitions(lifecycle_breadcrumb_test PRIVATE
    VAPORVIEW_LIFECYCLE_BREADCRUMB_TESTING=1
)
target_include_directories(lifecycle_breadcrumb_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(lifecycle_breadcrumb_test PRIVATE Qt6::Core)
add_test(NAME lifecycle_breadcrumb_test COMMAND lifecycle_breadcrumb_test)
set_tests_properties(lifecycle_breadcrumb_test PROPERTIES
    TIMEOUT 30
    LABELS "fast;unit;logging"
)
add_executable(log_fatal_helper
    ${CMAKE_CURRENT_SOURCE_DIR}/log_fatal_helper.cpp
)
target_include_directories(log_fatal_helper PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(log_fatal_helper PRIVATE
    vaporview_logging
    Qt6::Core
)
add_dependencies(log_service_test log_fatal_helper)

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

add_executable(sky_ipc_log_test
    ${CMAKE_CURRENT_SOURCE_DIR}/sky_ipc_log_test.cpp
)
target_include_directories(sky_ipc_log_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(sky_ipc_log_test PRIVATE
    vaporview_sky_core
    vaporview_sky_tui
    vaporview_logging
)
add_test(NAME sky_ipc_log_test COMMAND sky_ipc_log_test)

add_executable(sky_tui_log_model_test
    ${CMAKE_CURRENT_SOURCE_DIR}/sky_tui_log_model_test.cpp
)
target_include_directories(sky_tui_log_model_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_definitions(sky_tui_log_model_test PRIVATE
    VAPORVIEW_SKY_TUI_TESTING=1
)
target_link_libraries(sky_tui_log_model_test PRIVATE vaporview_sky_tui)
add_test(NAME sky_tui_log_model_test COMMAND sky_tui_log_model_test)
set_tests_properties(sky_tui_log_model_test PROPERTIES
    TIMEOUT 30
    LABELS "fast;unit;logging;sky-tui"
)
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

add_executable(application_config_test
    ${CMAKE_CURRENT_SOURCE_DIR}/application_config_test.cpp
)
target_link_libraries(application_config_test PRIVATE vaporview_app_config)
add_test(NAME application_config_test COMMAND application_config_test)

add_executable(rtk_serial_topology_test
    ${CMAKE_CURRENT_SOURCE_DIR}/rtk_serial_topology_test.cpp
)
target_link_libraries(rtk_serial_topology_test PRIVATE vaporview_ground_rtk)
add_test(NAME rtk_serial_topology_test COMMAND rtk_serial_topology_test)

add_executable(rtk_config_dialog_test
    ${CMAKE_CURRENT_SOURCE_DIR}/rtk_config_dialog_test.cpp
)
target_link_libraries(rtk_config_dialog_test PRIVATE vaporview_ground_rtk Qt6::Network)
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
target_include_directories(temperature_command_state_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(temperature_command_state_test PRIVATE
    vaporview_ground_temperature_command_state
    vaporview_ground_main_support
    Qt6::Widgets
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

add_executable(segmented_switch_button_test
    ${CMAKE_CURRENT_SOURCE_DIR}/segmented_switch_button_test.cpp
)
target_link_libraries(segmented_switch_button_test PRIVATE vaporview_ground_widgets)
add_test(NAME segmented_switch_button_test COMMAND segmented_switch_button_test)

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

add_executable(local_connection_coordinator_test
    ${CMAKE_CURRENT_SOURCE_DIR}/local_connection_coordinator_test.cpp
)
target_link_libraries(local_connection_coordinator_test PRIVATE vaporview_ground_device_core Qt6::Core)
add_test(NAME local_connection_coordinator_test COMMAND local_connection_coordinator_test)

add_executable(ui_test_mode_model_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_test_mode_model_test.cpp
)
target_link_libraries(ui_test_mode_model_test PRIVATE vaporview_ground_device_core Qt6::Core)
add_test(NAME ui_test_mode_model_test COMMAND ui_test_mode_model_test)

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
target_link_libraries(main_window_layout_test PRIVATE vaporview_main_window)
add_dependencies(main_window_layout_test VaporViewRuntimeResources)
if(VAPORVIEW_ENABLE_OSGEARTH AND WIN32)
    vaporview_stage_osgearth_runtime(main_window_layout_test)
endif()
add_custom_command(TARGET main_window_layout_test POST_BUILD
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
add_test(NAME home_overview_language_layout_test
    COMMAND main_window_layout_test --home-overview-language-only
)
add_test(NAME combination_navigation_page_test
    COMMAND main_window_layout_test --combination-navigation-only
)

add_executable(title_application_menu_test
    ${CMAKE_CURRENT_SOURCE_DIR}/title_application_menu_test.cpp
)
target_include_directories(title_application_menu_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(title_application_menu_test PRIVATE
    VAPORVIEW_MAIN_WINDOW_TESTING=1
)
target_link_libraries(title_application_menu_test PRIVATE vaporview_main_window)
add_dependencies(title_application_menu_test VaporViewRuntimeResources)
add_test(NAME title_application_menu_test COMMAND title_application_menu_test)
set_tests_properties(title_application_menu_test PROPERTIES
    TIMEOUT 120
    LABELS "integration;slow;ui;main-window;title-menu"
)

add_executable(ui_test_mode_window_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_test_mode_window_test.cpp
)
target_include_directories(ui_test_mode_window_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_test_mode_window_test PRIVATE vaporview_main_window Qt6::Network)
add_dependencies(ui_test_mode_window_test VaporViewRuntimeResources)
add_test(NAME ui_test_mode_window_test COMMAND ui_test_mode_window_test)

add_executable(update_check_dialog_test
    ${CMAKE_CURRENT_SOURCE_DIR}/update_check_dialog_test.cpp
)
target_include_directories(update_check_dialog_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(update_check_dialog_test PRIVATE
    vaporview_main_window
    Qt6::Network
)
add_dependencies(update_check_dialog_test VaporViewRuntimeResources)
if(VAPORVIEW_ENABLE_OSGEARTH AND WIN32)
    vaporview_stage_osgearth_runtime(update_check_dialog_test)
endif()
add_custom_command(TARGET update_check_dialog_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:update_check_dialog_test>/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/modern_style.qss
        $<TARGET_FILE_DIR:update_check_dialog_test>/resources/modern_style.qss
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_down.xpm
        $<TARGET_FILE_DIR:update_check_dialog_test>/resources/combo_arrow_down.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/resources/combo_arrow_up.xpm
        $<TARGET_FILE_DIR:update_check_dialog_test>/resources/combo_arrow_up.xpm
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/lucide
        $<TARGET_FILE_DIR:update_check_dialog_test>/resources/lucide
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/VaproViewLOGO
        $<TARGET_FILE_DIR:update_check_dialog_test>/resources/VaproViewLOGO
    COMMENT "Copying resources for update_check_dialog_test"
)
add_test(NAME update_check_dialog_test COMMAND update_check_dialog_test)

add_executable(ifw_update_status_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ifw_update_status_test.cpp
)
target_include_directories(ifw_update_status_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME ifw_update_status_test COMMAND ifw_update_status_test)

if(WIN32)
    add_executable(manifest_policy_test
        ${CMAKE_CURRENT_SOURCE_DIR}/manifest_policy_test.cpp
    )
    target_compile_definitions(manifest_policy_test PRIVATE
        VAPORVIEW_BINARY_DIR="${CMAKE_BINARY_DIR}"
        UNICODE
        _UNICODE
    )
    add_dependencies(manifest_policy_test
        VaporView
        VaporViewSky
        VaporViewSkyCore
        VaporViewSkyTui
        VaporViewUpdateRelauncher
        VaporViewPermissionTool
    )
    add_test(NAME manifest_policy_test COMMAND manifest_policy_test)
    set_tests_properties(manifest_policy_test PROPERTIES
        TIMEOUT 30
        LABELS "fast;integration;windows;manifest"
    )

    add_executable(permission_tool_test
        ${CMAKE_CURRENT_SOURCE_DIR}/permission_tool_test.cpp
    )
    target_compile_definitions(permission_tool_test PRIVATE
        VAPORVIEW_BINARY_DIR="${CMAKE_BINARY_DIR}"
        UNICODE
        _UNICODE
    )
    target_link_libraries(permission_tool_test PRIVATE advapi32)
    add_dependencies(permission_tool_test VaporViewPermissionTool)
    add_test(NAME permission_tool_test COMMAND permission_tool_test)
    set_tests_properties(permission_tool_test PROPERTIES
        TIMEOUT 120
        LABELS "integration;windows;acl"
    )

    add_executable(relauncher_probe_child
        ${CMAKE_CURRENT_SOURCE_DIR}/relauncher_probe_child.cpp
    )
    target_compile_definitions(relauncher_probe_child PRIVATE UNICODE _UNICODE)

    add_executable(update_relauncher_elevation_test
        ${CMAKE_CURRENT_SOURCE_DIR}/update_relauncher_elevation_test.cpp
    )
    target_compile_definitions(update_relauncher_elevation_test PRIVATE
        VAPORVIEW_BINARY_DIR="${CMAKE_BINARY_DIR}"
        UNICODE
        _UNICODE
    )
    add_dependencies(update_relauncher_elevation_test
        VaporViewUpdateRelauncher
        relauncher_probe_child
    )
    add_test(NAME update_relauncher_standard_parent_test
        COMMAND update_relauncher_elevation_test standard
    )
    add_test(NAME update_relauncher_elevated_parent_test
        COMMAND update_relauncher_elevation_test elevated
    )
    set_tests_properties(update_relauncher_standard_parent_test PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        LABELS "integration;windows;relauncher"
    )
    set_tests_properties(update_relauncher_elevated_parent_test PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        LABELS "integration;windows;relauncher"
    )

    add_test(NAME ifw_staging_test
        COMMAND powershell -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/ifw_staging_test.ps1
                -StageDir ${CMAKE_BINARY_DIR}/ifw-work/stage
                -WorkDir ${CMAKE_BINARY_DIR}/ifw-work
    )
    set_tests_properties(ifw_staging_test PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 60
        LABELS "integration;windows;ifw"
    )
endif()

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

    add_executable(map_resource_manifest_test
        ${CMAKE_CURRENT_SOURCE_DIR}/map_resource_manifest_test.cpp
    )
    target_include_directories(map_resource_manifest_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(map_resource_manifest_test PRIVATE vaporview_map3d_osgearth)
    add_test(NAME map_resource_manifest_test COMMAND map_resource_manifest_test)

    add_executable(map_resource_download_test
        ${CMAKE_CURRENT_SOURCE_DIR}/map_resource_download_test.cpp
    )
    target_include_directories(map_resource_download_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(map_resource_download_test PRIVATE
        vaporview_map3d_osgearth
        Qt6::Network
    )
    add_test(NAME map_resource_download_test COMMAND map_resource_download_test)

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
set_tests_properties(vaporview_startup_test PROPERTIES
    SKIP_RETURN_CODE 77
)

add_executable(startup_splash_test
    ${CMAKE_CURRENT_SOURCE_DIR}/startup_splash_test.cpp
    ${CMAKE_SOURCE_DIR}/src/app/StartupSplash.cpp
    ${CMAKE_SOURCE_DIR}/src/app/StartupSplash.h
    ${CMAKE_SOURCE_DIR}/resources/vaporview_startup.qrc
)
target_include_directories(startup_splash_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(startup_splash_test PRIVATE
    vaporview_logging
    Qt6::Widgets
    Qt6::Svg
)
add_test(NAME startup_splash_test COMMAND startup_splash_test)

set(VAPORVIEW_FAST_UNIT_TESTS
    trajectory_buffer_test
    trajectory_quality_test
    trajectory_heatmap_test
    trajectory_replay_test
    session_track_reader_test
    coordinate_transform_test
    height_reference_test
    tcp_wave_encoding_test
    temperature_controller_protocol_test
    ai8_temperature_controller_protocol_test
    ai8_temperature_controller_collector_test
    ai8_temperature_controller_panel_test
    lifecycle_breadcrumb_test
    telemetry_codec_test
    telemetry_tcp_link_test
    sky_session_recorder_test
    sky_local_ipc_client_test
    visual_text_label_test
    segmented_switch_button_test
    session_time_format_test
    device_rate_policy_test
    application_config_test
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
    ui_test_mode_model_test
    imu_configuration_service_test
    serial_port_detection_service_test
    single_level_popup_menu_test
)
set_tests_properties(${VAPORVIEW_FAST_UNIT_TESTS} PROPERTIES TIMEOUT 30)
set_property(TEST ${VAPORVIEW_FAST_UNIT_TESTS} PROPERTY LABELS fast unit)
set_property(TEST lifecycle_breadcrumb_test APPEND PROPERTY LABELS logging)

set(VAPORVIEW_FAST_INTEGRATION_TESTS
    map_data_script_test
    device_panel_coordinator_test
    tcp_wave_panel_test
    sky_device_config_dialog_test
    startup_splash_test
    vaporview_startup_test
)
set_tests_properties(${VAPORVIEW_FAST_INTEGRATION_TESTS} PROPERTIES TIMEOUT 60)
set_property(TEST ${VAPORVIEW_FAST_INTEGRATION_TESTS} PROPERTY LABELS fast integration)

set(VAPORVIEW_SLOW_UI_TESTS
    main_window_layout_test
    home_overview_language_layout_test
    combination_navigation_page_test
    ui_test_mode_window_test
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
set_property(TEST segmented_switch_button_test APPEND PROPERTY LABELS ground-widgets ui)
set_property(TEST single_level_popup_menu_test APPEND PROPERTY LABELS ui menu accessibility)
set_property(TEST ai8_temperature_controller_panel_test APPEND PROPERTY LABELS ground-widgets ui)
set_property(TEST
    temperature_controller_protocol_test
    ai8_temperature_controller_protocol_test
    ai8_temperature_controller_collector_test
    epsilon_protocol_test
    telemetry_codec_test
    telemetry_tcp_link_test
    APPEND PROPERTY LABELS protocol
)
set_property(TEST
    main_window_layout_test
    home_overview_language_layout_test
    session_viewer_io_test
    session_viewer_window_state_test
    session_viewer_trajectory_test
    session_viewer_theme_test
    session_viewer_components_test
    startup_splash_test
    vaporview_startup_test
    APPEND PROPERTY LABELS ui
)
set_property(TEST main_window_layout_test APPEND PROPERTY LABELS main-window layout)
set_property(TEST home_overview_language_layout_test APPEND PROPERTY LABELS main-window layout)
set_property(TEST combination_navigation_page_test APPEND PROPERTY LABELS main-window layout)
set_property(TEST ui_test_mode_window_test APPEND PROPERTY LABELS main-window ui-test-mode)
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
        map_resource_manifest_test
        map_resource_download_test
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
