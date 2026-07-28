cmake_minimum_required(VERSION 3.1)

project(libresource CXX)

aux_source_directory(. LIB_RESOURCE_SRCS)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../libs)

set(CMAKE_CXX_FLAGS "-Wall -std=c++14 -pthread -DEPOLL -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -O0 -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

add_library(${PROJECT_NAME} STATIC ${LIB_RESOURCE_SRCS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${PROJECT_NAME} PUBLIC libserver)
