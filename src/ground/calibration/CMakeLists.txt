add_library(vaporview_water_calibration_core STATIC WaterCalibration.cpp WaterCalibration.h ConcentrationResults.cpp
    CalibrationProject.cpp CalibrationProject.h CalibrationFile.cpp CalibrationFile.h
    RealtimePeakBus.cpp RealtimePeakBus.h CalibrationService.cpp CalibrationService.h
    RecordedConcentrations.cpp RecordedConcentrations.h)
set_target_properties(vaporview_water_calibration_core PROPERTIES AUTOMOC ON)
target_include_directories(vaporview_water_calibration_core PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(vaporview_water_calibration_core PUBLIC vaporview_ground_session_core vaporview_app_config Qt6::Core)

add_library(vaporview_water_calibration_ui STATIC WaterCalibrationPage.cpp WaterCalibrationPage.h
    CalibrationReviewPlot.cpp CalibrationReviewPlot.h)
target_include_directories(vaporview_water_calibration_ui PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(vaporview_water_calibration_ui PUBLIC vaporview_water_calibration_core Qt6::Widgets
    PRIVATE vaporview_app_theme vaporview_logging Qt6::Concurrent)
