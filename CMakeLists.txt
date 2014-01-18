cmake_minimum_required(VERSION 2.8)

project(arena)

add_executable(arena main.cc rendering.cc lodepng/lodepng.cc tinycthread/tinycthread.c)

add_definitions(-O3)

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
