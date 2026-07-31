cmake_minimum_required(VERSION 3.14)
project(geoscandecoder)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
    sgp4
    GIT_REPOSITORY https://github.com/aholinch/sgp4.git
    GIT_TAG        552cb1489a52c3023ae70cb6c7e239e84c5950fe
)

FetchContent_GetProperties(sgp4)
if(NOT sgp4_POPULATED)
    FetchContent_Populate(sgp4)
endif()

set(geoscandecoder_SOURCES
    geoscandecoder.cpp
    geoscandecoderplugin.cpp
    geoscandecoderbaseband.cpp
    geoscandecodersettings.cpp
    dsp.cpp
    tle_utils.cpp
    ${sgp4_SOURCE_DIR}/src/cpp/SGP4.c
    ${sgp4_SOURCE_DIR}/src/cpp/TLE.cpp
)

set_source_files_properties(${sgp4_SOURCE_DIR}/src/cpp/SGP4.c PROPERTIES LANGUAGE CXX)

if(MSVC)
    # SGP4.c defines its own fmod() which clashes with the MSVC intrinsic
    set_source_files_properties(${sgp4_SOURCE_DIR}/src/cpp/SGP4.c PROPERTIES COMPILE_DEFINITIONS "fmod=sgp4_fmod")
endif()

set(geoscandecoder_HEADERS
    geoscandecoder.h
    geoscandecoderplugin.h
    geoscandecoderbaseband.h
    geoscandecodersettings.h
    dsp.hpp
    tle_utils.hpp
)

set(geoscandecoder_FORMS)

if(NOT SERVER_MODE)
    set(geoscandecoder_SOURCES
        ${geoscandecoder_SOURCES}
        geoscandecoder_gui.cpp
    )
    set(geoscandecoder_HEADERS
        ${geoscandecoder_HEADERS}
        geoscandecoder_gui.h
    )
    set(geoscandecoder_FORMS
        geoscandecoder.ui
    )
    set(TARGET_NAME ${PLUGINS_PREFIX}geoscandecoder)
    set(TARGET_LIB "Qt::Widgets")
    set(TARGET_LIB_GUI "sdrgui")
    set(INSTALL_FOLDER ${INSTALL_PLUGINS_DIR})
else()
    set(TARGET_NAME ${PLUGINSSRV_PREFIX}geoscandecodersrv)
    set(TARGET_LIB "")
    set(TARGET_LIB_GUI "")
    set(INSTALL_FOLDER ${INSTALL_PLUGINSSRV_DIR})
endif()

if(NOT Qt6_FOUND)
    add_library(${TARGET_NAME} ${geoscandecoder_SOURCES})
else()
    qt_add_plugin(${TARGET_NAME} CLASS_NAME GEOSCANDecoderPlugin ${geoscandecoder_SOURCES})
endif()

if(NOT BUILD_SHARED_LIBS)
    set_property(GLOBAL APPEND PROPERTY STATIC_PLUGINS_PROPERTY ${TARGET_NAME})
endif()

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/swagger/sdrangel/code/qt5/client
    ${sgp4_SOURCE_DIR}/src/cpp
)

target_link_libraries(${TARGET_NAME} PRIVATE
    Qt::Core
    ${TARGET_LIB}
    sdrbase
    ${TARGET_LIB_GUI}
)

target_compile_options(${TARGET_NAME} PRIVATE 
    -O3
    -ffast-math
    -march=native
)

install(TARGETS ${TARGET_NAME} DESTINATION ${INSTALL_FOLDER})

# Install debug symbols
if (WIN32)
    install(FILES $<TARGET_PROPERTY:${TARGET_NAME},RUNTIME_OUTPUT_DIRECTORY>/${TARGET_NAME}stripped.pdb CONFIGURATIONS Release DESTINATION ${INSTALL_FOLDER} RENAME ${TARGET_NAME}.pdb)
    install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}> CONFIGURATIONS Debug RelWithDebInfo DESTINATION ${INSTALL_FOLDER})
endif()
