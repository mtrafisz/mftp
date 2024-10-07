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
