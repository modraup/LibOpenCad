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
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${CONSUMER_SRC}"
    -B "${consumer_build}"
    "-DCMAKE_PREFIX_PATH=${PREFIX}"
    "-DPROBE_SRC=${PROBE_SRC}"
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
