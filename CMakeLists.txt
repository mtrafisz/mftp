cmake_minimum_required(VERSION 3.25)
project(mftp C)

set(CMAKE_C_STANDARD 99)

set(EXTERNAL_PATH ${CMAKE_SOURCE_DIR}/external)

link_directories(${EXTERNAL_PATH}/lib)
include_directories(${EXTERNAL_PATH}/include)

set(LIBUEV ${EXTERNAL_PATH}/lib/libuev.a)

file(GLOB_RECURSE SHARED_SRC src/shared/*.c* src/shared/*.h*)
add_library(mftp-shared ${SHARED_SRC})

include_directories(src)

file(GLOB_RECURSE SERVER_SRC src/server/*.c* src/server/*.h*)
add_executable(mftp-server ${SERVER_SRC})
target_link_libraries(mftp-server mftp-shared ${LIBUEV})

file(GLOB_RECURSE CLIENT_CLI_SRC src/client-cli/*.c* src/client-cli/*.h*)
add_executable(mftp-client-cli ${CLIENT_CLI_SRC})
target_link_libraries(mftp-client-cli mftp-shared)


# Define user and group
set(MFTP_USER "mftp")
set(MFTP_GROUP "mftp")

# Configure setup.bash
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/setup.bash.in
    ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    @ONLY
)

# Set executable permissions for setup.bash
file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/setup.bash PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)

# Install setup.bash
install(
    PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/setup.bash
    DESTINATION /usr/local/bin
    RENAME mftp-setup.bash
)

# Execute setup.bash during installation
install(CODE "
    execute_process(
        COMMAND /usr/local/bin/mftp-setup.bash
        RESULT_VARIABLE setup_result
        OUTPUT_VARIABLE setup_output
        ERROR_VARIABLE setup_error
    )
    if(NOT setup_result EQUAL 0)
        message(FATAL_ERROR \"mftp-setup.bash failed: \${setup_error}\")
    endif()
")

# Configure mftp.service
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/systemd/mftp.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/mftp.service
    @ONLY
)

# Install mftp.service
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/mftp.service
    DESTINATION /etc/systemd/system
)

# Install the mftp-server binary
install(
    TARGETS mftp-server
    RUNTIME DESTINATION /usr/bin
)

# Reload systemd, enable, and start the service
install(CODE "
    execute_process(
        COMMAND systemctl daemon-reload
        RESULT_VARIABLE reload_result
        OUTPUT_VARIABLE reload_output
        ERROR_VARIABLE reload_error
    )
    if(NOT reload_result EQUAL 0)
        message(FATAL_ERROR \"systemctl daemon-reload failed: \${reload_error}\")
    endif()

    execute_process(
        COMMAND systemctl enable mftp.service
        RESULT_VARIABLE enable_result
        OUTPUT_VARIABLE enable_output
        ERROR_VARIABLE enable_error
    )
    if(NOT enable_result EQUAL 0)
        message(FATAL_ERROR \"systemctl enable mftp.service failed: \${enable_error}\")
    endif()

    execute_process(
        COMMAND systemctl start mftp.service
        RESULT_VARIABLE start_result
        OUTPUT_VARIABLE start_output
        ERROR_VARIABLE start_error
    )
    if(NOT start_result EQUAL 0)
        message(FATAL_ERROR \"systemctl start mftp.service failed: \${start_error}\")
    endif()
")

if (NOT TARGET uninstall)
    configure_file (
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake
        @ONLY
    )

    add_custom_target (uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake
    )
endif ()
