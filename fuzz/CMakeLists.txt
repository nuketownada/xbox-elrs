cmake_minimum_required(VERSION 3.16)
project(xbox-elrs-fuzz C)

# Require clang for libFuzzer support
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Clang is required for fuzzing (got ${CMAKE_C_COMPILER_ID})")
endif()

set(CMAKE_C_STANDARD 11)

# Common flags: AddressSanitizer + UBSan for all targets
set(SANITIZE_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZE_FLAGS} -g -O1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZE_FLAGS}")

# Stubs directory comes first so our stubs.h shadows ESP-IDF headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Also add main/ for project headers (crsf.h, channel_mixer.h, etc.)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../main)

# libFuzzer flag for fuzz targets only
set(FUZZER_FLAGS "-fsanitize=fuzzer")

# Fuzz target: USB report parser
add_executable(fuzz_parse_report fuzz_parse_report.c)
target_compile_options(fuzz_parse_report PRIVATE ${FUZZER_FLAGS})
target_link_options(fuzz_parse_report PRIVATE ${FUZZER_FLAGS})
target_link_libraries(fuzz_parse_report m)

# Fuzz target: channel mixer
add_executable(fuzz_mixer fuzz_mixer.c)
target_compile_options(fuzz_mixer PRIVATE ${FUZZER_FLAGS})
target_link_options(fuzz_mixer PRIVATE ${FUZZER_FLAGS})
target_link_libraries(fuzz_mixer m)

# Fuzz target: CRSF channel packing
add_executable(fuzz_pack_channels fuzz_pack_channels.c)
target_compile_options(fuzz_pack_channels PRIVATE ${FUZZER_FLAGS})
target_link_options(fuzz_pack_channels PRIVATE ${FUZZER_FLAGS})
target_link_libraries(fuzz_pack_channels m)

# Deterministic watchdog test (NOT a fuzzer — regular executable)
add_executable(test_watchdog test_watchdog.c)
target_link_libraries(test_watchdog m)
