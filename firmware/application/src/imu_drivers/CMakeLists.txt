target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(CONFIG_MIDGE_SEL_IMU_ICM20948)
    add_subdirectory(icm20948)
elseif(CONFIG_MIDGE_SEL_IMU_ICM42605)
    add_subdirectory(icm42605)
else()
    message(FATAL_ERROR "No IMU selected in MIDGE_SEL_IMU")
endif()
