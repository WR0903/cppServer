cmake_minimum_required(VERSION 3.1)

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

# 只收集 allinone 目录下的源码
aux_source_directory(. SRCS)

# 收集其他 app 目录下的源码（排除各自的 main.cpp）
aux_source_directory(../appmgr APPMGR_SRCS)
list(REMOVE_ITEM APPMGR_SRCS "../appmgr/main.cpp")

aux_source_directory(../login LOGIN_SRCS)
list(REMOVE_ITEM LOGIN_SRCS "../login/main.cpp")

aux_source_directory(../dbmgr DBMGR_SRCS)
list(REMOVE_ITEM DBMGR_SRCS "../dbmgr/main.cpp")

aux_source_directory(../game GAME_SRCS)
list(REMOVE_ITEM GAME_SRCS "../game/main.cpp")

aux_source_directory(../space SPACE_SRCS)
list(REMOVE_ITEM SPACE_SRCS "../space/main.cpp")

set(ALL_SRCS ${SRCS} ${APPMGR_SRCS} ${LOGIN_SRCS} ${DBMGR_SRCS} ${GAME_SRCS} ${SPACE_SRCS})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../bin)

set(CMAKE_CXX_FLAGS "-Wall -std=c++14 -pthread -DEPOLL -Wno-unknown-pragmas -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -O0 -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

add_executable(${MyProjectName} ${ALL_SRCS})

target_include_directories(${MyProjectName} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libs
    ${CMAKE_SOURCE_DIR}/src/libs/libserver
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/protobuf
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/mongoose
    ${CMAKE_SOURCE_DIR}/src/libs/libplayer
    ${CMAKE_SOURCE_DIR}/src/libs/libresource
    ${CMAKE_SOURCE_DIR}/src/apps
)

target_link_libraries(${MyProjectName} libplayer libresource libserver jsoncpp yaml-cpp log4cplusS libprotobuf libcurl_static hiredis mysqlclient pthread m ssl crypto dl zlibstatic uuid)
