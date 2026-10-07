# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(badge-esp32)


add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/rls
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${PROJECT_BIN} ${CMAKE_BINARY_DIR}/rls/${PROJECT_BIN}
    COMMENT "Copying esp32 binary output to SomeFolder"
)

add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/phy_init_data.bin ${CMAKE_BINARY_DIR}/rls/phy_init_data.bin
    COMMENT "Copying esp32 binary output to SomeFolder"
)
add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin ${CMAKE_BINARY_DIR}/rls/bootloader.bin
    COMMENT "Copying esp32 binary output to SomeFolder"
)
add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin ${CMAKE_BINARY_DIR}/rls/partition-table.bin
    COMMENT "Copying esp32 binary output to SomeFolder"
)
