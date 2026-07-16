set(UM982_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/um982_driver)
set(HIPNUC_DRIVER_DIR ${CMAKE_SOURCE_DIR}/third_party/hipnuc_driver)
set(RTKLIB_DIR ${CMAKE_SOURCE_DIR}/third_party/rtklib)
set(RTKLIB_STR2STR_SOURCE ${RTKLIB_DIR}/app/consapp/str2str/str2str.c)

set(RTKLIB_SOURCES
    ${RTKLIB_DIR}/src/stream.c
    ${RTKLIB_DIR}/src/streamsvr.c
    ${RTKLIB_DIR}/src/rtkcmn.c
    ${RTKLIB_DIR}/src/solution.c
    ${RTKLIB_DIR}/src/sbas.c
    ${RTKLIB_DIR}/src/geoid.c
    ${RTKLIB_DIR}/src/rcvraw.c
    ${RTKLIB_DIR}/src/rtcm.c
    ${RTKLIB_DIR}/src/rtcm2.c
    ${RTKLIB_DIR}/src/rtcm3.c
    ${RTKLIB_DIR}/src/rtcm3e.c
    ${RTKLIB_DIR}/src/preceph.c
    ${RTKLIB_DIR}/src/rcv/novatel.c
    ${RTKLIB_DIR}/src/rcv/ublox.c
    ${RTKLIB_DIR}/src/rcv/ss2.c
    ${RTKLIB_DIR}/src/rcv/crescent.c
    ${RTKLIB_DIR}/src/rcv/skytraq.c
    ${RTKLIB_DIR}/src/rcv/javad.c
    ${RTKLIB_DIR}/src/rcv/nvs.c
    ${RTKLIB_DIR}/src/rcv/binex.c
    ${RTKLIB_DIR}/src/rcv/rt17.c
    ${RTKLIB_DIR}/src/rcv/septentrio.c
)

add_library(rtklib_strsvr STATIC ${RTKLIB_SOURCES})
target_include_directories(rtklib_strsvr PUBLIC ${RTKLIB_DIR}/src)
target_compile_definitions(rtklib_strsvr PRIVATE
    ENAGLO
    ENAGAL
    ENAQZS
    ENACMP
    ENAIRN
    NFREQ=5
    NEXOBS=3
    SVR_REUSEADDR
)

if(MSVC)
    target_compile_definitions(rtklib_strsvr PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rtklib_strsvr PRIVATE /W0 /Zc:__cplusplus /permissive-)
else()
    target_compile_options(rtklib_strsvr PRIVATE -w)
endif()

target_link_libraries(rtklib_strsvr PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(rtklib_strsvr PUBLIC ws2_32 winmm)
elseif(UNIX)
    target_link_libraries(rtklib_strsvr PUBLIC m)
endif()

if(EXISTS ${RTKLIB_STR2STR_SOURCE})
    add_executable(str2str ${RTKLIB_STR2STR_SOURCE})
    target_include_directories(str2str PRIVATE ${RTKLIB_DIR}/src)
    target_link_libraries(str2str PRIVATE rtklib_strsvr)

    if(MSVC)
        target_compile_definitions(str2str PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(str2str PRIVATE /W0 /utf-8 /Zc:__cplusplus /permissive-)
    else()
        target_compile_options(str2str PRIVATE -w)
    endif()
endif()
