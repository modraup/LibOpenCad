cmake_minimum_required(VERSION 3.16)
project(CadExample LANGUAGES CXX)

find_package(LibOpenCad CONFIG REQUIRED)
add_executable(create_drawing create_drawing.cpp)
target_link_libraries(create_drawing PRIVATE LibOpenCad::libopencad)

enable_testing()
add_test(NAME create_drawing COMMAND create_drawing)

get_target_property(sdk_type LibOpenCad::libopencad TYPE)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND sdk_type STREQUAL "STATIC_LIBRARY")
    add_subdirectory(macos_static_plugin)
endif()
