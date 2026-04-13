cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(blinky)

# Get the current git commit hash
execute_process(
    COMMAND uv run hatch version
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE HATCH_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# get the current unix timestamp
string(TIMESTAMP FW_BUILD_TIMESTAMP "%s" UTC)


target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# Add the commit hash as a compile definition
add_compile_definitions(FW_VERSION="${HATCH_VERSION}")
add_compile_definitions(FW_BUILD_TIMESTAMP=${FW_BUILD_TIMESTAMP}ULL)

add_subdirectory(src/imu_drivers)

target_sources(app PRIVATE
    src/main.c
    src/storage.c
    src/proximity.c
    src/audio.c
    src/imu.c
    src/battery_charge.c
    src/time_control.c
    src/cmd_processor.c
    src/privacy_switch.c
    src/status_led.c
    )
