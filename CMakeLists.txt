cmake_minimum_required(VERSION 2.8)

project(arena)

add_executable(arena main.cc lz4.c lz4.h util.hh util.cc auto.hh callstack.hh rendering.cc rendering.hh lodepng/lodepng.cc tinycthread/tinycthread.c

jansson/dump.c			jansson/hashtable.c		jansson/load.c			jansson/pack_unpack.c		jansson/strconv.c		jansson/value.c
jansson/error.c			jansson/hashtable_seed.c	jansson/memory.c		jansson/strbuffer.c		jansson/utf.c
jansson/hashtable.h		jansson/jansson.h		jansson/jansson_private.h	jansson/lookup3.h		jansson/strbuffer.h		jansson/utf.h
)

add_definitions(-g -O3 -Wno-c++11-extensions -flto)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x -DNDEBUG")

add_subdirectory(glfw)
include_directories(glfw/include)
include_directories(lodepng)
include_directories(tinycthread)
include_directories(jansson)

if (APPLE)
    target_link_libraries(arena glfw ${GLFW_LIBRARIES})
else()
    find_package(GLEW REQUIRED)
    target_link_libraries(arena glfw dl ${GLEW_LIBRARIES} ${GLFW_LIBRARIES})
endif()
