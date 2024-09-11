cmake_minimum_required (VERSION 3.15)
project (mftp)

add_executable (test-server source/test-server.c)
target_link_libraries(test-server pthread)

add_executable (test-client source/test-client.c)
