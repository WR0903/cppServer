cmake_minimum_required(VERSION 3.1)

if (CMAKE_BUILD_TYPE STREQUAL Release) 
	set(MyProjectName robots)
else()
	set(CMAKE_BUILD_TYPE Debug)
	set(MyProjectName robotsd)
endif()

message(STATUS "CMAKE_BINARY_DIR:${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE}")
message(STATUS "ProjectName:${MyProjectName}")

project(${MyProjectName})

# 只收集本目录下的源码
aux_source_directory(. SRCS)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../bin)

set(CMAKE_CXX_FLAGS "-Wall -pthread -DEPOLL -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -O0 -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

add_executable(${MyProjectName} ${SRCS})

target_include_directories(${MyProjectName} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libserver
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libserver/protobuf
    ${CMAKE_SOURCE_DIR}/src/3rdparty/mongoose
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libplayer
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libresource
    ${CMAKE_SOURCE_DIR}/src/libs
    ${CMAKE_SOURCE_DIR}/src/apps
)

target_link_libraries(${MyProjectName} libplayer libresource libserver jsoncpp yaml-cpp log4cplusS libprotobuf libcurl_static hiredis zlibstatic uuid)
