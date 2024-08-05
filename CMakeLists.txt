cmake_minimum_required(VERSION 3.11)

project(OpenFBX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

include(GNUInstallDirs)
include(FetchContent)

# Fetch libdeflate
FetchContent_Declare(
        libdeflate
        GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
        GIT_TAG v1.10  # Beispiel-Tag, ändere dies auf die gewünschte Version
)
FetchContent_MakeAvailable(libdeflate)

# Define the targets for libdeflate
add_library(libdeflate_static STATIC IMPORTED)
set_target_properties(libdeflate_static PROPERTIES
        IMPORTED_LOCATION "${libdeflate_SOURCE_DIR}/libdeflate.a"
        INTERFACE_INCLUDE_DIRECTORIES "${libdeflate_SOURCE_DIR}"
)

add_library(libdeflate_shared SHARED IMPORTED)
set_target_properties(libdeflate_shared PROPERTIES
        IMPORTED_LOCATION "${libdeflate_SOURCE_DIR}/libdeflate.so"
        INTERFACE_INCLUDE_DIRECTORIES "${libdeflate_SOURCE_DIR}"
)

add_library(OpenFBX src/ofbx.cpp)
target_link_libraries(OpenFBX PRIVATE libdeflate_static)

target_include_directories(OpenFBX
        PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        PRIVATE
        ${libdeflate_SOURCE_DIR}
)

target_compile_definitions(OpenFBX PRIVATE _LARGEFILE64_SOURCE)
set_target_properties(OpenFBX PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        POSITION_INDEPENDENT_CODE ON)

install(TARGETS OpenFBX EXPORT openfbxTargets
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        INCLUDES DESTINATION include)

include(CMakePackageConfigHelpers)
configure_package_config_file(
        openfbxConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/openfbxConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/openfbx"
)

install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/openfbxConfig.cmake"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/openfbx"
)

install(
        EXPORT openfbxTargets
        FILE openfbxTargets.cmake
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/openfbx
)

install(FILES ${CMAKE_SOURCE_DIR}/src/ofbx.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
