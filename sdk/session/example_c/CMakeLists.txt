cmake_minimum_required(VERSION 3.16)
project(VaporViewSessionCExample LANGUAGES C CXX)

find_package(VaporViewSessionSdk CONFIG REQUIRED COMPONENTS C)

add_executable(vaporview_session_c_example main.c)
set_target_properties(vaporview_session_c_example PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_link_libraries(vaporview_session_c_example PRIVATE VaporView::SessionSdkC)

add_executable(vaporview_session_cpp_example main.cpp)
set_target_properties(vaporview_session_cpp_example PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
target_link_libraries(vaporview_session_cpp_example PRIVATE VaporView::SessionSdkC)
if(MINGW)
    target_link_options(vaporview_session_c_example PRIVATE -municode)
    target_link_options(vaporview_session_cpp_example PRIVATE -municode)
endif()
