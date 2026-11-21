cmake_minimum_required(VERSION 3.16)
project(MacDwgPlugin LANGUAGES CXX)

find_package(LibOpenCad 0.1 CONFIG REQUIRED)
get_target_property(sdk_type LibOpenCad::libopencad TYPE)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR NOT sdk_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Select the macOS static SDK with CMAKE_PREFIX_PATH.")
endif()

add_library(dwg_plugin MODULE plugin.cpp)
target_link_libraries(dwg_plugin PRIVATE LibOpenCad::libopencad)
set_target_properties(dwg_plugin PROPERTIES PREFIX "" SUFFIX ".bundle")
target_link_options(dwg_plugin PRIVATE "LINKER:-exported_symbol,_cad_roundtrip")

add_executable(dwg_host host.cpp)
target_compile_features(dwg_host PRIVATE cxx_std_17)
target_link_libraries(dwg_host PRIVATE ${CMAKE_DL_LIBS})

enable_testing()
add_test(NAME macos_plugin_dwg_roundtrip
    COMMAND dwg_host "$<TARGET_FILE:dwg_plugin>" "${CMAKE_CURRENT_BINARY_DIR}/roundtrip.dwg")
