cmake_minimum_required (VERSION 3.15)
project (mftp)

file (GLOB_RECURSE LIB_PROTO_SOURCES "./source/proto/*.c*")
add_library(${PROJECT_NAME} ${LIB_PROTO_SOURCES})

add_executable("server_${PROJECT_NAME}" source/server.c)
target_link_libraries("server_${PROJECT_NAME}" ${PROJECT_NAME} pthread)

add_executable("test_client" source/test-client.c)
target_link_libraries(${PROJECT_NAME})

add_executable("test_dumper" source/test-dumper.c)
target_link_libraries(${PROJECT_NAME})
