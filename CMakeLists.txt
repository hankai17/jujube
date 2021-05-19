cmake_minimum_required(VERSION 3.7)
PROJECT(test)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -DDEBUG_MEM_CHECK -DUSE_FASTLZ")

include_directories("./"
        )

find_library(THREAD pthread /usr/lib64/)

set(LIBS ${PROJECT_NAME}
        pthread
        dl
        )

add_executable( test
        fastlz.c 
        ip.c
        buf.c
        mem.c
        comm.c
        event.c
        stream.c
        connection.c
        client.c
        )

TARGET_LINK_LIBRARIES(${PROJECT_NAME} pthread)

