cmake_minimum_required(VERSION 2.8)

project(arena)

add_executable(arena main.cc util.hh util.cc auto.hh callstack.hh rendering.cc rendering.hh lodepng/lodepng.cc tinycthread/tinycthread.c)

add_definitions(-g -Wno-c++11-extensions)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")

add_subdirectory(glfw)
include_directories(glfw/include)
include_directories(lodepng)
include_directories(tinycthread)
include_directories(rocksdb/include)

add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/rocksdb/librocksdb.dylib COMMAND make shared_lib -j WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/rocksdb)

set(ROCKSDB ${CMAKE_SOURCE_DIR}/rocksdb/librocksdb.dylib)

if (APPLE)
    target_link_libraries(arena glfw ${GLFW_LIBRARIES} ${ROCKSDB})
else()
    find_package(GLEW REQUIRED)
    target_link_libraries(arena glfw dl ${GLEW_LIBRARIES} ${GLFW_LIBRARIES} ${ROCKSDB})
endif()
