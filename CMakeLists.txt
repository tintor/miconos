cmake_minimum_required(VERSION 2.8)

project(arena)

add_executable(arena main.cc lz4.c lz4.h util.hh util.cc auto.hh callstack.hh rendering.cc rendering.hh lodepng/lodepng.cc tinycthread/tinycthread.c)

add_definitions(-g -O3 -Wno-c++11-extensions -flto)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x -DNDEBUG")

add_subdirectory(glfw)
include_directories(glfw/include)
include_directories(lodepng)
include_directories(tinycthread)

if (APPLE)
    target_link_libraries(arena glfw ${GLFW_LIBRARIES})
else()
    find_package(GLEW REQUIRED)
    target_link_libraries(arena glfw dl ${GLEW_LIBRARIES} ${GLFW_LIBRARIES})
endif()
