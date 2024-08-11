cmake_minimum_required(VERSION 3.28)
project(app C)

set(CMAKE_C_STANDARD 23)

# Set compiler flags for debugging
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Wall -Wextra")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra")
endif()

add_executable(
        app ./main.c
        ../include/event_loop.h
        ../include/protocol.h
        ../include/transport.h
        ../include/reactor.h
        ../src/event_loop.c
        ../src/epoll_loop.c
        ../src/tcp_transport.c
        ../src/reactor.c
        ../include/base.h
        ../include/epoll_loop.h
        ../include/list.h
)

# Link pthread library
target_link_libraries(app PRIVATE pthread)
