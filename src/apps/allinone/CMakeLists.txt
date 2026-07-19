cmake_minimum_required(VERSION 2.8)

if (CMAKE_BUILD_TYPE STREQUAL Release) 
	set(MyProjectName allinone)
else()
	set(CMAKE_BUILD_TYPE Debug)
	set(MyProjectName allinoned)
endif()

message(STATUS "CMAKE_BINARY_DIR:${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE}")
message(STATUS "ProjectName:${MyProjectName}")

project(${MyProjectName})

aux_source_directory(. SRCS)

aux_source_directory(../appmgr SRCS)
list(REMOVE_ITEM SRCS "../appmgr/main.cpp")

aux_source_directory(../login SRCS)
list(REMOVE_ITEM SRCS "../login/main.cpp")

aux_source_directory(../dbmgr SRCS)
list(REMOVE_ITEM SRCS "../dbmgr/main.cpp")

aux_source_directory(../game SRCS)
list(REMOVE_ITEM SRCS "../game/main.cpp")

aux_source_directory(../space SRCS)
list(REMOVE_ITEM SRCS "../space/main.cpp")

aux_source_directory(../../libs/libserver SRCS)
aux_source_directory(../../libs/libserver/protobuf SRCS)
aux_source_directory(../../libs/libserver/mongoose SRCS)
aux_source_directory(../../libs/libplayer SRCS)
aux_source_directory(../../libs/libresource SRCS)

include_directories(../../libs)
include_directories(../../libs/libserver)
include_directories(../../libs/libserver/protobuf)
include_directories(../../libs/libserver/mongoose)
include_directories(../../apps)
include_directories(/home/ubuntu/GameBookServer/dependence/jsoncpp-1.9.1/include)
include_directories(../../../../include/common)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../bin)

set(CMAKE_CXX_FLAGS "-Wall -std=c++14 -pthread -DEPOLL -Wno-unknown-pragmas")
set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -O0 -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

link_directories(${CMAKE_SOURCE_DIR}/../../../libs)
link_directories(${CMAKE_SOURCE_DIR}/../../../../libs/linux)
link_directories(/home/ubuntu/GameBookServer/dependence/jsoncpp-1.9.1/build)

add_executable(${MyProjectName} ${SRCS})
target_link_libraries(${MyProjectName} protobuf jsoncpp curl mysqlclient pthread z m ssl crypto dl yaml-cpp log4cplus hiredis uuid)
