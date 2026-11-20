# Install the just-built tree and compile a find_package consumer against it.
if(NOT DEFINED BUILD_DIR OR NOT DEFINED PREFIX OR NOT DEFINED CONSUMER_SRC OR NOT DEFINED PROBE_SRC)
    message(FATAL_ERROR "BUILD_DIR, PREFIX, CONSUMER_SRC and PROBE_SRC are required")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}"
    RESULT_VARIABLE status)
if(status)
    message(FATAL_ERROR "cmake --install failed (${status})")
endif()
set(consumer_build "${PREFIX}/consumer-build")
# Use the producing compiler and instrumentation settings. In particular, a
# static ASan/UBSan library requires the consumer to link the same runtimes.
# Includes and dependencies still come exclusively from the installed package.
load_cache("${BUILD_DIR}" READ_WITH_PREFIX producer_
    CMAKE_CXX_COMPILER CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_BUILD_TYPE
    CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
    CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_RELEASE
    CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO CMAKE_EXE_LINKER_FLAGS_MINSIZEREL)
set(consumer_options
    "-DCMAKE_CXX_COMPILER=${producer_CMAKE_CXX_COMPILER}"
    "-DCMAKE_CXX_FLAGS=${producer_CMAKE_CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${producer_CMAKE_EXE_LINKER_FLAGS}"
    "-DCMAKE_BUILD_TYPE=${producer_CMAKE_BUILD_TYPE}")
foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    list(APPEND consumer_options
        "-DCMAKE_CXX_FLAGS_${config}=${producer_CMAKE_CXX_FLAGS_${config}}"
        "-DCMAKE_EXE_LINKER_FLAGS_${config}=${producer_CMAKE_EXE_LINKER_FLAGS_${config}}")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${CONSUMER_SRC}"
    -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${PREFIX}"
    "-DPROBE_SRC=${PROBE_SRC}"
    ${consumer_options}
    RESULT_VARIABLE status)
if(status)
    message(FATAL_ERROR "consumer configure failed (${status})")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
    RESULT_VARIABLE status)
if(status)
    message(FATAL_ERROR "consumer build failed (${status})")
endif()
set(probe "${consumer_build}/stage8_package_probe")
if(NOT EXISTS "${probe}")
    set(probe "${consumer_build}/Release/stage8_package_probe")
endif()
if(NOT EXISTS "${probe}")
    set(probe "${consumer_build}/Debug/stage8_package_probe")
endif()
if(NOT EXISTS "${probe}" AND WIN32)
    set(probe "${consumer_build}/stage8_package_probe.exe")
endif()
if(NOT EXISTS "${probe}")
    message(FATAL_ERROR "consumer probe executable missing")
endif()
execute_process(COMMAND "${probe}"
    WORKING_DIRECTORY "${PREFIX}"
    RESULT_VARIABLE status)
if(status)
    message(FATAL_ERROR "consumer probe failed (${status})")
endif()
if(NOT EXISTS "${PREFIX}/drawing.dxf")
    message(FATAL_ERROR "consumer probe did not write drawing.dxf")
endif()
