target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/imu_interface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/icm20948_util_func.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $ENV{ZEPHYR_BASE}/drivers/sensor/tdk/icm20948
)
